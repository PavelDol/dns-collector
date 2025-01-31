/*
 *	UCW Library -- Single-Linked Lists
 *
 *	(c) 2005 Martin Mares <mj@ucw.cz>
 *	(c) 2017 Pavel Charvat <pchar@ucw.cz>
 *
 *	This software may be freely distributed and used according to the terms
 *	of the GNU Lesser General Public License.
 */

#ifndef _UCW_SLISTS_H
#define _UCW_SLISTS_H

#ifdef CONFIG_UCW_CLEAN_ABI
#define slist_insert_before ucw_slist_insert_before
#define slist_prev ucw_slist_prev
#define slist_remove ucw_slist_remove
#endif

/**
 * Common header for list nodes.
 **/
typedef struct snode {
  struct snode *next;
} snode;

/**
 * Single-linked list.
 **/
typedef struct slist {
  struct snode head, *last;
} slist;

/**
 * Initialize a new single-linked list. Must be called before any other function.
 **/
static inline void slist_init(slist *l)
{
  l->head.next = l->last = NULL;
}

/**
 * Return the first node of @l or NULL if @l is empty.
 **/
static inline void *slist_head(slist *l)
{
  return l->head.next;
}

/**
 * Return the last node of @l or NULL if @l is empty.
 **/
static inline void *slist_tail(slist *l)
{
  return l->last;
}

/**
 * Find the next node to @n or NULL if @n is the last one.
 **/
static inline void *slist_next(snode *n)
{
  return n->next;
}

/**
 * Return a non-zero value iff @l is empty.
 **/
static inline int slist_empty(slist *l)
{
  return !l->head.next;
}

/**
 * Insert a new node in front of all other nodes.
 **/
static inline void slist_add_head(slist *l, snode *n)
{
  n->next = l->head.next;
  l->head.next = n;
  if (!l->last)
    l->last = n;
}

/**
 * Insert a new node after all other nodes.
 **/
static inline void slist_add_tail(slist *l, snode *n)
{
  if (l->last)
    l->last->next = n;
  else
    l->head.next = n;
  n->next = NULL;
  l->last = n;
}

/**
 * Insert a new node just after the node @after. To insert a new head, use @slist_add_head() instead.
 **/
static inline void slist_insert_after(slist *l, snode *what, snode *after)
{
  what->next = after->next;
  after->next = what;
  if (!what->next)
    l->last = what;
}

/**
 * Quickly remove the node next to @after. The node may not exist.
 **/
static inline void slist_remove_after(slist *l, snode *after)
{
  snode *n = after->next;
  if (n)
    {
      after->next = n->next;
      if (l->last == n)
        l->last = (after == &l->head) ? NULL : after;
    }
}

/**
 * Remove the first node in @l. The list can be empty.
 **/
static inline void *slist_remove_head(slist *l)
{
  snode *n = slist_head(l);
  if (n)
    slist_remove_after(l, &l->head);
  return n;
}

/* Loops */

/**
 * Loop over all nodes in the @list and perform the next C statement on them. The current node is stored in @n which must be defined before as pointer to any type.
 * The list should not be changed during this loop command.
 **/
#define SLIST_WALK(n,list) for(n=(void*)(list).head.next; (n); (n)=(void*)((snode*)(n))->next)

/**
 * Same as @SLIST_WALK(), but allows removal of the current node. This macro requires one more variable to store the pointer to the previous node (useful for @slist_remove_after()).
 **/
#define SLIST_WALK_DELSAFE(n,list,prev) for((prev)=(void*)&(list).head; (n)=(void*)((snode*)prev)->next; (prev)=(((snode*)(prev))->next==(snode*)(n) ? (void*)(n) : (void*)(prev)))

/**
 * Same as @SLIST_WALK(), but it defines the variable for the current node in place. @type should be a pointer type.
 **/
#define SLIST_FOR_EACH(type,n,list) for(type n=(void*)(list).head.next; n; n=(void*)((snode*)(n))->next)

/* Non-trivial functions */

/**
 * Find the previous node to @n or NULL if @n is the first one. Beware linear time complexity.
 **/
void *slist_prev(slist *l, snode *n);

/**
 * Insert a new node just before the node @before. To insert a new tail, use @slist_add_tail(). Beware linear time complexity.
 **/
void slist_insert_before(slist *l, snode *what, snode *before);

/**
 * Remove node @n. Beware linear time complexity.
 **/
void slist_remove(slist *l, snode *n);

/**
 * Remove the last node in @l. The list can be empty.
 **/
static inline void slist_remove_tail(slist *l)
{
  slist_remove(l, l->last);
}

/**
 * Merge two lists by inserting the list @what in front of all other nodes in a different list @l.
 * The first list is then cleared.
 **/
static inline void slist_add_list_head(slist *l, slist *what)
{
  if (!slist_empty(what))
    {
      if (!slist_empty(l))
	what->last->next = l->head.next;
      else
	l->last = what->last;
      l->head.next = what->head.next;
      slist_init(what);
    }
}

/**
 * Merge two lists by inserting the list @what after all other nodes in a different list @l.
 * The first list is then cleared.
 **/
static inline void slist_add_list_tail(slist *l, slist *what)
{
  if (!slist_empty(what))
    {
      if (!slist_empty(l))
	l->last->next = what->head.next;
      else
	l->head.next = what->head.next;
      l->last = what->last;
      slist_init(what);
    }
}

/**
 * Move all items from a source list to a destination list. The source list
 * becomes empty, the original contents of the destination list are destroyed.
 **/
static inline void slist_move(slist *to, slist *from)
{
  to->head.next = from->head.next;
  to->last = from->last;
  slist_init(from);
}

/**
 * Compute the number of nodes in @l. Beware linear time complexity.
 **/
static inline uint slist_size(slist *l)
{
  uint i = 0;
  SLIST_FOR_EACH(snode *, n, *l)
    i++;
  return i;
}

#endif
