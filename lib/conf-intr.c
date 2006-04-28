/*
 *	UCW Library -- Configuration files: interpreter
 *
 *	(c) 2001--2006 Robert Spalek <robert@ucw.cz>
 *	(c) 2003--2006 Martin Mares <mj@ucw.cz>
 *
 *	This software may be freely distributed and used according to the terms
 *	of the GNU Lesser General Public License.
 */

#include "lib/lib.h"
#include "lib/conf.h"
#include "lib/getopt.h"
#include "lib/conf-internal.h"
#include "lib/clists.h"

#include <string.h>
#include <stdio.h>

#define TRY(f)	do { byte *_msg = f; if (_msg) return _msg; } while (0)

/* Register size of and parser for each basic type */

static byte *
cf_parse_string(byte *str, byte **ptr)
{
  *ptr = cf_strdup(str);
  return NULL;
}

typedef byte *cf_basic_parser(byte *str, void *ptr);
static struct {
  uns size;
  void *parser;
} parsers[] = {
  { sizeof(int), cf_parse_int },
  { sizeof(u64), cf_parse_u64 },
  { sizeof(double), cf_parse_double },
  { sizeof(u32), cf_parse_ip },
  { sizeof(byte*), cf_parse_string },
  { sizeof(int), NULL },			// lookups are parsed extra
  { 0, NULL },					// user-defined types are parsed extra
};

inline uns
cf_type_size(enum cf_type type, struct cf_user_type *utype)
{
  if (type < CT_USER)
    return parsers[type].size;
  else
    return utype->size;
}

static byte *
cf_parse_lookup(byte *str, int *ptr, byte **t)
{
  byte **n = t;
  uns total_len = 0;
  while (*n && strcasecmp(*n, str)) {
    total_len += strlen(*n) + 2;
    n++;
  }
  if (*n) {
    *ptr = n - t;
    return NULL;
  }
  byte *err = cf_malloc(total_len + strlen(str) + 60), *c = err;
  c += sprintf(err, "Invalid value %s, possible values are: ", str);
  for (n=t; *n; n++)
    c+= sprintf(c, "%s, ", *n);
  if (*t)
    c[-2] = 0;
  *ptr = -1;
  return err;
}

static byte *
cf_parse_ary(uns number, byte **pars, void *ptr, enum cf_type type, union cf_union *u)
{
  for (uns i=0; i<number; i++)
  {
    byte *msg;
    uns size = cf_type_size(type, u->utype);
    if (type < CT_LOOKUP)
      msg = ((cf_basic_parser*) parsers[type].parser) (pars[i], ptr + i * size);
    else if (type == CT_LOOKUP)
      msg = cf_parse_lookup(pars[i], ptr + i * size, u->lookup);
    else if (type == CT_USER)
      msg = u->utype->parser(pars[i], ptr + i * size);
    else
      ASSERT(0);
    if (msg)
      return cf_printf("Cannot parse item %d: %s", i+1, msg);
  }
  return NULL;
}

/* Interpreter */

#define T(x) #x,
byte *cf_op_names[] = { CF_OPERATIONS };
#undef T

static byte *
interpret_set_dynamic(struct cf_item *item, int number, byte **pars, void **ptr)
{
  enum cf_type type = item->type;
  cf_journal_block(ptr, sizeof(void*));
  // boundary checks done by the caller
  uns size = cf_type_size(item->type, item->u.utype);
  ASSERT(size >= sizeof(uns));
  *ptr = cf_malloc((number+1) * size) + size;
  * (uns*) (*ptr - size) = number;
  return cf_parse_ary(number, pars, *ptr, type, &item->u);
}

static byte *
interpret_add_dynamic(struct cf_item *item, int number, byte **pars, int *processed, void **ptr, enum cf_operation op)
{
  enum cf_type type = item->type;
  void *old_p = *ptr;
  uns size = cf_type_size(item->type, item->u.utype);
  ASSERT(size >= sizeof(uns));
  int old_nr = old_p ? * (int*) (old_p - size) : 0;
  int taken = MIN(number, ABS(item->number)-old_nr);
  *processed = taken;
  // stretch the dynamic array
  void *new_p = cf_malloc((old_nr + taken + 1) * size) + size;
  * (uns*) (new_p - size) = old_nr + taken;
  cf_journal_block(ptr, sizeof(void*));
  *ptr = new_p;
  if (op == OP_APPEND) {
    memcpy(new_p, old_p, old_nr * size);
    return cf_parse_ary(taken, pars, new_p + old_nr * size, type, &item->u);
  } else if (op == OP_PREPEND) {
    memcpy(new_p + taken * size, old_p, old_nr * size);
    return cf_parse_ary(taken, pars, new_p, type, &item->u);
  } else
    return cf_printf("Dynamic arrays do not support operation %s", cf_op_names[op]);
}

static byte *interpret_set_item(struct cf_item *item, int number, byte **pars, int *processed, void *ptr, uns allow_dynamic);

static byte *
interpret_section(struct cf_section *sec, int number, byte **pars, int *processed, void *ptr, uns allow_dynamic)
{
  cf_add_dirty(sec, ptr);
  *processed = 0;
  for (struct cf_item *ci=sec->cfg; ci->cls; ci++)
  {
    int taken;
    byte *msg = interpret_set_item(ci, number, pars, &taken, ptr + (addr_int_t) ci->ptr, allow_dynamic && !ci[1].cls);
    if (msg)
      return cf_printf("Item %s: %s", ci->name, msg);
    *processed += taken;
    number -= taken;
    pars += taken;
    if (!number)		// stop parsing, because many parsers would otherwise complain that number==0
      break;
  }
  return NULL;
}

static void
add_to_list(cnode *where, cnode *new_node, enum cf_operation op)
{
  switch (op)
  {
    case OP_EDIT:		// edition has been done in-place
      break;
    case OP_REMOVE:
      CF_JOURNAL_VAR(where->prev->next);
      CF_JOURNAL_VAR(where->next->prev);
      clist_remove(where);
      break;
    case OP_AFTER:		// implementation dependend (prepend_head = after(list)), and where==list, see clists.h:74
    case OP_PREPEND:
    case OP_COPY:
      CF_JOURNAL_VAR(where->next->prev);
      CF_JOURNAL_VAR(where->next);
      clist_insert_after(new_node, where);
      break;
    case OP_BEFORE:		// implementation dependend (append_tail = before(list))
    case OP_APPEND:
    case OP_SET:
      CF_JOURNAL_VAR(where->prev->next);
      CF_JOURNAL_VAR(where->prev);
      clist_insert_before(new_node, where);
      break;
    default:
      ASSERT(0);
  }
}

static byte *
interpret_add_list(struct cf_item *item, int number, byte **pars, int *processed, void *ptr, enum cf_operation op)
{
  if (op >= OP_REMOVE)
    return cf_printf("You have to open a block for operation %s", cf_op_names[op]);
  if (!number)
    return "Nothing to add to the list";
  struct cf_section *sec = item->u.sec;
  *processed = 0;
  while (number > 0)
  {
    void *node = cf_malloc(sec->size);
    cf_init_section(item->name, sec, node, 1);
    add_to_list(ptr, node, op);
    int taken;
    /* If the node contains any dynamic attribute at the end, we suppress
     * auto-repetition here and pass the flag inside instead.  */
    TRY( interpret_section(sec, number, pars, &taken, node, sec->flags & SEC_FLAG_DYNAMIC) );
    *processed += taken;
    number -= taken;
    pars += taken;
    if (sec->flags & SEC_FLAG_DYNAMIC)
      break;
  }
  return NULL;
}

static byte *
interpret_set_item(struct cf_item *item, int number, byte **pars, int *processed, void *ptr, uns allow_dynamic)
{
  int taken;
  switch (item->cls)
  {
    case CC_STATIC:
      if (!number)
	return "Missing value";
      taken = MIN(number, item->number);
      *processed = taken;
      uns size = cf_type_size(item->type, item->u.utype);
      cf_journal_block(ptr, taken * size);
      return cf_parse_ary(taken, pars, ptr, item->type, &item->u);
    case CC_DYNAMIC:
      if (!allow_dynamic)
	return "Dynamic array cannot be used here";
      taken = MIN(number, ABS(item->number));
      *processed = taken;
      return interpret_set_dynamic(item, taken, pars, ptr);
    case CC_PARSER:
      if (item->number < 0 && !allow_dynamic)
	return "Parsers with variable number of parameters cannot be used here";
      if (item->number > 0 && number < item->number)
	return "Not enough parameters available for the parser";
      taken = MIN(number, ABS(item->number));
      *processed = taken;
      for (int i=0; i<taken; i++)
	pars[i] = cf_strdup(pars[i]);
      return item->u.par(taken, pars, ptr);
    case CC_SECTION:
      return interpret_section(item->u.sec, number, pars, processed, ptr, allow_dynamic);
    case CC_LIST:
      if (!allow_dynamic)
	return "Lists cannot be used here";
      return interpret_add_list(item, number, pars, processed, ptr, OP_SET);
    default:
      ASSERT(0);
  }
}

static byte *
interpret_clear(struct cf_item *item, void *ptr)
{
  if (item->cls == CC_LIST) {
    cf_journal_block(ptr, sizeof(clist));
    clist_init(ptr);
  } else if (item->cls == CC_DYNAMIC) {
    cf_journal_block(ptr, sizeof(void *));
    * (void**) ptr = NULL;
  } else
    return "The item is not a list or a dynamic array";
  return NULL;
}

static int
cmp_items(void *i1, void *i2, struct cf_item *item)
{
  ASSERT(item->cls == CC_STATIC);
  i1 += (addr_int_t) item->ptr;
  i2 += (addr_int_t) item->ptr;
  if (item->type == CT_STRING)
    return strcmp(* (byte**) i1, * (byte**) i2);
  else				// all numeric types
    return memcmp(i1, i2, cf_type_size(item->type, item->u.utype));
}

static void *
find_list_node(clist *list, void *query, struct cf_section *sec, u32 mask)
{
  CLIST_FOR_EACH(cnode *, n, *list)
  {
    uns found = 1;
    for (uns i=0; i<32; i++)
      if (mask & (1<<i))
	if (cmp_items(n, query, sec->cfg+i))
	{
	  found = 0;
	  break;
	}
    if (found)
      return n;
  }
  return NULL;
}

static byte *
record_selector(struct cf_item *item, struct cf_section *sec, u32 *mask)
{
  uns nr = sec->flags & SEC_FLAG_NUMBER;
  if (item >= sec->cfg && item < sec->cfg + nr)	// setting an attribute relative to this section
  {
    uns i = item - sec->cfg;
    if (i >= 32)
      return "Cannot select list nodes by this attribute";
    if (sec->cfg[i].cls != CC_STATIC)
      return "Selection can only be done based on basic attributes";
    *mask |= 1 << i;
  }
  return NULL;
}

#define MAX_STACK_SIZE	10
static struct item_stack {
  struct cf_section *sec;	// nested section
  void *base_ptr;		// because original pointers are often relative
  enum cf_operation op;		// it is performed when a closing brace is encountered
  void *list;			// list the operations should be done on
  u32 mask;			// bit array of selectors searching in a list
  struct cf_item *item;		// cf_item of the list
} stack[MAX_STACK_SIZE];
static uns level;

static byte *
opening_brace(struct cf_item *item, void *ptr, enum cf_operation op)
{
  if (level >= MAX_STACK_SIZE-1)
    return "Too many nested sections";
  stack[++level] = (struct item_stack) {
    .sec = NULL,
    .base_ptr = NULL,
    .op = op & OP_MASK,
    .list = NULL,
    .mask = 0,
    .item = NULL,
  };
  if (!item)			// unknown is ignored; we just need to trace recursion
    return NULL;
  stack[level].sec = item->u.sec;
  if (item->cls == CC_SECTION)
  {
    stack[level].base_ptr = ptr;
    stack[level].op = OP_EDIT | OP_2ND;	// this list operation does nothing
  }
  else if (item->cls == CC_LIST)
  {
    stack[level].base_ptr = cf_malloc(item->u.sec->size);
    cf_init_section(item->name, item->u.sec, stack[level].base_ptr, 1);
    stack[level].list = ptr;
    stack[level].item = item;
    if ((op & OP_MASK) < OP_REMOVE) {
      add_to_list(ptr, stack[level].base_ptr, op & OP_MASK);
      stack[level].op |= OP_2ND;
    } else
      stack[level].op |= OP_1ST;
  }
  else
    return "Opening brace can only be used on sections and lists";
  return NULL;
}

static byte *
closing_brace(struct item_stack *st, enum cf_operation op, int number, byte **pars)
{
  if (st->op == OP_CLOSE)	// top-level
    return "Unmatched } parenthesis";
  if (!st->sec) {		// dummy run on unknown section
    if (!(op & OP_OPEN))
      level--;
    return NULL;
  }
  enum cf_operation pure_op = st->op & OP_MASK;
  if (st->op & OP_1ST)
  {
    st->list = find_list_node(st->list, st->base_ptr, st->sec, st->mask);
    if (!st->list)
      return "Cannot find a node matching the query";
    if (pure_op != OP_REMOVE)
    {
      if (pure_op == OP_EDIT)
	st->base_ptr = st->list;
      else if (pure_op == OP_AFTER || pure_op == OP_BEFORE)
	cf_init_section(st->item->name, st->sec, st->base_ptr, 1);
      else if (pure_op == OP_COPY) {
	if (st->sec->flags & SEC_FLAG_CANT_COPY)
	  return cf_printf("Item %s cannot be copied", st->item->name);
	memcpy(st->base_ptr, st->list, st->sec->size);	// strings and dynamic arrays are shared
	if (st->sec->copy)
	  TRY( st->sec->copy(st->base_ptr, st->list) );
      } else
	ASSERT(0);
      if (op & OP_OPEN) {	// stay at the same recursion level
	st->op = (st->op | OP_2ND) & ~OP_1ST;
	add_to_list(st->list, st->base_ptr, pure_op);
	return NULL;
      }
      int taken;		// parse parameters on 1 line immediately
      TRY( interpret_section(st->sec, number, pars, &taken, st->base_ptr, 1) );
      number -= taken;
      pars += taken;
      // and fall-thru to the 2nd phase
    }
    add_to_list(st->list, st->base_ptr, pure_op);
  }
  level--;
  if (number)
    return "No parameters expected after the }";
  else if (op & OP_OPEN)
    return "No { is expected";
  else
    return NULL;
}

static struct cf_item *
find_item(struct cf_section *curr_sec, byte *name, byte **msg, void **ptr)
{
  *msg = NULL;
  if (name[0] == '^')				// absolute name instead of relative
    name++, curr_sec = &cf_sections, *ptr = NULL;
  if (!curr_sec)				// don't even search in an unknown section
    return NULL;
  while (1)
  {
    if (curr_sec != &cf_sections)
      cf_add_dirty(curr_sec, *ptr);
    byte *c = strchr(name, '.');
    if (c)
      *c++ = 0;
    struct cf_item *ci = cf_find_subitem(curr_sec, name);
    if (!ci->cls)
    {
      if (!(curr_sec->flags & SEC_FLAG_UNKNOWN))	// ignore silently unknown top-level sections and unknown attributes in flagged sections
	*msg = cf_printf("Unknown item %s", name);
      return NULL;
    }
    *ptr += (addr_int_t) ci->ptr;
    if (!c)
      return ci;
    if (ci->cls != CC_SECTION)
    {
      *msg = cf_printf("Item %s is not a section", name);
      return NULL;
    }
    curr_sec = ci->u.sec;
    name = c;
  }
}

byte *
cf_interpret_line(byte *name, enum cf_operation op, int number, byte **pars)
{
  byte *msg;
  if ((op & OP_MASK) == OP_CLOSE)
    return closing_brace(stack+level, op, number, pars);
  void *ptr = stack[level].base_ptr;
  struct cf_item *item = find_item(stack[level].sec, name, &msg, &ptr);
  if (msg)
    return msg;
  if (stack[level].op & OP_1ST)
    TRY( record_selector(item, stack[level].sec, &stack[level].mask) );
  if (op & OP_OPEN) {		// the operation will be performed after the closing brace
    if (number)
      return "Cannot open a block after a parameter has been passed on a line";
    return opening_brace(item, ptr, op);
  }
  if (!item)			// ignored item in an unknown section
    return NULL;
  op &= OP_MASK;

  int taken;			// process as many parameters as possible
  if (op == OP_CLEAR)
    taken = 0, msg = interpret_clear(item, ptr);
  else if (op == OP_SET)
    msg = interpret_set_item(item, number, pars, &taken, ptr, 1);
  else if (item->cls == CC_DYNAMIC)
    msg = interpret_add_dynamic(item, number, pars, &taken, ptr, op);
  else if (item->cls == CC_LIST)
    msg = interpret_add_list(item, number, pars, &taken, ptr, op);
  else
    return cf_printf("Operation %s not supported on attribute %s", cf_op_names[op], name);
  if (msg)
    return msg;
  if (taken < number)
    return cf_printf("Too many parameters: %d>%d", number, taken);

  return NULL;
}

byte *
cf_find_item(byte *name, struct cf_item *item)
{
  byte *msg;
  void *ptr;
  struct cf_item *ci = find_item(&cf_sections, name, &msg, &ptr);
  if (msg)
    return msg;
  if (ci) {
    *item = *ci;
    item->ptr = ptr;
  } else
    bzero(item, sizeof(struct cf_item));
  return NULL;
}

byte *
cf_write_item(struct cf_item *item, enum cf_operation op, int number, byte **pars)
{
  byte *msg;
  int taken;
  switch (op) {
    case OP_SET:
      msg = interpret_set_item(item, number, pars, &taken, item->ptr, 1);
      break;
    case OP_CLEAR:
      taken = 0;
      msg = interpret_clear(item, item->ptr);
      break;
    case OP_APPEND:
    case OP_PREPEND:
      if (item->cls == CC_DYNAMIC)
	msg = interpret_add_dynamic(item, number, pars, &taken, item->ptr, op);
      else if (item->cls == CC_LIST)
	msg = interpret_add_list(item, number, pars, &taken, item->ptr, op);
      else
	return "The attribute class does not support append/prepend";
      break;
    default:
      return "Unsupported operation";
  }
  if (msg)
    return msg;
  if (taken < number)
    return "Too many parameters";
  return NULL;
}

void
cf_init_stack(void)
{
  static uns initialized = 0;
  if (!initialized++) {
    cf_sections.flags |= SEC_FLAG_UNKNOWN;
    cf_sections.size = 0;			// size of allocated array used to be stored here
    cf_init_section(NULL, &cf_sections, NULL, 0);
  }
  level = 0;
  stack[0] = (struct item_stack) {
    .sec = &cf_sections,
    .base_ptr = NULL,
    .op = OP_CLOSE,
    .list = NULL,
    .mask = 0,
    .item = NULL
  };
}

int
cf_check_stack(void)
{
  if (level > 0) {
    log(L_ERROR, "Unterminated block");
    return 1;
  }
  return 0;
}

