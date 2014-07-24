/*
 *	UCW Library -- Table printer
 *
 *	(c) 2014 Robert Kessl <robert.kessl@economia.cz>
 */

#ifndef _UCW_TABLE_H
#define _UCW_TABLE_H

#include <inttypes.h>

#include <ucw/fastbuf.h>
#include <ucw/mempool.h>
#include <ucw/xtypes.h>

// FIXME: update these macros
#ifdef CONFIG_UCW_CLEAN_ABI
#define table_append_bool ucw_table_append_bool
#define table_append_double ucw_table_append_double
#define table_append_int ucw_table_append_int
#define table_append_intmax ucw_table_append_intmax
#define table_append_printf ucw_table_append_printf
#define table_append_str ucw_table_append_str
#define table_append_u64 ucw_table_append_u64
#define table_append_uint ucw_table_append_uint
#define table_append_uintmax ucw_table_append_uintmax
#define table_cleanup ucw_table_cleanup
#define table_col_bool ucw_table_col_bool
#define table_col_bool_fmt ucw_table_col_bool_fmt
#define table_col_bool_name ucw_table_col_bool_name
#define table_col_double ucw_table_col_double
#define table_col_double_fmt ucw_table_col_double_fmt
#define table_col_double_name ucw_table_col_double_name
#define table_col_fbend ucw_table_col_fbend
#define table_col_fbstart ucw_table_col_fbstart
#define table_col_int ucw_table_col_int
#define table_col_int_fmt ucw_table_col_int_fmt
#define table_col_int_name ucw_table_col_int_name
#define table_col_intmax ucw_table_col_intmax
#define table_col_intmax_fmt ucw_table_col_intmax_fmt
#define table_col_intmax_name ucw_table_col_intmax_name
#define table_col_printf ucw_table_col_printf
#define table_col_s64 ucw_table_col_s64
#define table_col_s64_fmt ucw_table_col_s64_fmt
#define table_col_s64_name ucw_table_col_s64_name
#define table_col_str ucw_table_col_str
#define table_col_str_fmt ucw_table_col_str_fmt
#define table_col_str_name ucw_table_col_str_name
#define table_col_u64 ucw_table_col_u64
#define table_col_u64_fmt ucw_table_col_u64_fmt
#define table_col_u64_name ucw_table_col_u64_name
#define table_col_uint ucw_table_col_uint
#define table_col_uint_fmt ucw_table_col_uint_fmt
#define table_col_uint_name ucw_table_col_uint_name
#define table_col_uintmax ucw_table_col_uintmax
#define table_col_uintmax_fmt ucw_table_col_uintmax_fmt
#define table_col_uintmax_name ucw_table_col_uintmax_name
#define table_end ucw_table_end
#define table_end_row ucw_table_end_row
#define table_fmt_blockline ucw_table_fmt_blockline
#define table_fmt_human_readable ucw_table_fmt_human_readable
#define table_fmt_machine_readable ucw_table_fmt_machine_readable
#define table_get_col_idx ucw_table_get_col_idx
#define table_get_col_list ucw_table_get_col_list
#define table_init ucw_table_init
#define table_set_col_order ucw_table_set_col_order
#define table_set_col_order_by_name ucw_table_set_col_order_by_name
#define table_set_formatter ucw_table_set_formatter
#define table_set_gary_options ucw_table_set_gary_options
#define table_set_option ucw_table_set_option
#define table_set_option_value ucw_table_set_option_value
#define table_start ucw_table_start
#endif

/***
 * Table definitions
 * -----------------
 ***/

// FIXME: update documentation according to the changes made in recent commits!

/** The COL_TYPE_ANY macro specifies a column type which can be filled with arbitrary type. **/

#define COL_TYPE_ANY      NULL

/** Justify cell contents to the left. **/
#define CELL_ALIGN_LEFT     (1U << 31)

// CELL_FLAG_MASK has 1's in bits used for column flags,
// CELL_WIDTH_MASK has 1's in bits used for column width.
#define CELL_FLAG_MASK	(CELL_ALIGN_LEFT)
#define CELL_WIDTH_MASK	(~CELL_FLAG_MASK)

struct table;

/**
 * Definition of a single table column.
 * Usually, this is generated using the `TABLE_COL_`'type' macros.
 * Fields marked with `[*]` are user-accessible.
 **/
struct table_column {
  const char *name;		// [*] Name of the column displayed in table header
  uint width;			// [*] Width of the column (in characters) OR'ed with column flags
  uint fmt;                     // [*] default format of the column
  const struct xtype *type_def; // [*] pointer to xtype of this column

  const char * (*set_col_opt)(struct table *tbl, uint col, const char *value);
       // [*] process table option for a column instance
};

// FIXME: is it correct to have idx and col_def? idx is sufficient and in fact a duplicity of idx
// idx is used only for initialization and col_def is used in other cases
struct table_col_instance {
  uint idx;                            // idx is a index into struct table::columns
  const struct table_column *col_def;  // this is pointer to the column definition, located in the array struct table::columns
  const char *cell_content;            // content of the cell of the current row
  int next_column;                     // index of next column in linked list of columns of the same type
  uint fmt;                            // format of this column
};

/**
 * Definition of a table. Contains column definitions, and some per-table settings.
 * Please use only fields marked with `[*]`.
 **/
struct table_template {
  const struct table_column *columns;       // [*] Definition of columns
  struct table_col_instance *column_order;  // [*] Order of the columns in the print-out of the table
  uint cols_to_output;                      // [*] Number of columns that are printed
  const char *col_delimiter;                // [*] Delimiter that is placed between columns
  // Back-end used for table formatting and its private data
  const struct table_formatter *formatter; // FIXME: should be const?
};

/**
 * Handle of a table. Contains column definitions, per-table settings
 * and internal data. To change the table definition, please use only
 * fields marked with `[*]`.
 **/
struct table {
  const struct table_column *columns;	// [*] Definition of columns
  int column_count;			// [*] Number of columns (calculated by table_init())
  int *ll_headers;                      // headers of linked lists that connects column instances
  struct mempool *pool;			// Memory pool used for storing table data. Contains global state
					// and data of the current row.
  struct mempool_state pool_state;	// State of the pool after the table is initialized, i.e., before
					// per-row data have been allocated.

  struct table_col_instance *column_order;  // [*] Order of the columns in the print-out of the table
  uint cols_to_output;			// [*] Number of columns that are printed
  const char *col_delimiter;		// [*] Delimiter that is placed between columns
  bool print_header;			// [*] false indicates that table header should not be printed

  struct fastbuf *out;			// [*] Fastbuffer to which the table is printed
  int last_printed_col;			// Index of the last column which was set. -1 indicates start of row.
					// Used for example for appending to the current column.
  bool row_printing_started;		// Indicates that a row has been started. Duplicates last_printed_col, but harmlessly.
  struct fbpool fb_col_out;		// Per-cell fastbuf, see table_col_fbstart()
  int col_out;				// Index of the column that is currently printed using fb_col_out

  // Back-end used for table formatting and its private data
  const struct table_formatter *formatter;
  void *data;
};

/****
 * In most cases, table descriptions are constructed using the following macros.
 * See the examples above for more details.
 *
 *  * `TBL_COLUMNS` indicates the start of definition of columns
 *  * `TBL_COL_`'type'`(name, width)` defines a column of a given type
 *  * `TBL_COL_`'type'`_FMT(name, width, fmt)` defines a column with a custom format string
 *  * `TBL_COL_END` ends the column definitions
 *  * `TBL_COL_ORDER` specifies custom ordering of columns in the output
 *  * `TBL_COL_DELIMITER` and `TBL_APPEND_DELIMITER` override default delimiters
 *  * `TBL_FMT_HUMAN_READABLE` requests human-readable formatting (this is the default)
 *  * `TBL_FMT_MACHINE_READABLE` requests machine-readable TSV output
 *  * `TBL_FMT_BLOCKLINE` requests block formatting (each cell printed a pair of a key and value on its own line)
 *
 ***/

#define TBL_COL_STR(_name, _width)            { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_str }
#define TBL_COL_INT(_name, _width)            { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_int }
#define TBL_COL_S64(_name, _width)            { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_s64 }
#define TBL_COL_UINT(_name, _width)           { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_uint }
#define TBL_COL_U64(_name, _width)            { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_u64 }
#define TBL_COL_INTMAX(_name, _width)         { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_intmax }
#define TBL_COL_UINTMAX(_name, _width)        { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_uintmax }
#define TBL_COL_HEXUINT(_name, _width)        { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_uint }
#define TBL_COL_DOUBLE(_name, _width)         { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_double }
#define TBL_COL_BOOL(_name, _width)           { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = &xt_bool }
#define TBL_COL_ANY(_name, _width)            { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = COL_TYPE_ANY }
#define TBL_COL_CUSTOM(_name, _width, _xtype) { .name = _name, .width = _width, .fmt = XTYPE_FMT_DEFAULT, .type_def = _xtype }

#define TBL_COL_STR_FMT(_name, _width, _fmt)            { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_str }
#define TBL_COL_INT_FMT(_name, _width, _fmt)            { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_int }
#define TBL_COL_S64_FMT(_name, _width, _fmt)            { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_s64 }
#define TBL_COL_UINT_FMT(_name, _width, _fmt)           { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_uint }
#define TBL_COL_U64_FMT(_name, _width, _fmt)            { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_u64 }
#define TBL_COL_INTMAX_FMT(_name, _width, _fmt)         { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_intmax }
#define TBL_COL_UINTMAX_FMT(_name, _width, _fmt)        { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_uintmax }
#define TBL_COL_HEXUINT_FMT(_name, _width, _fmt)        { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_uint }
#define TBL_COL_BOOL_FMT(_name, _width, _fmt)           { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_bool }
#define TBL_COL_ANY_FMT(_name, _width, _fmt)            { .name = _name, .width = _width, .fmt = _fmt, .type_def = COL_TYPE_ANY }
#define TBL_COL_DOUBLE_FMT(_name, _width, _fmt)         { .name = _name, .width = _width, .fmt = _fmt, .type_def = &xt_double }

#define TBL_COL_END { .name = 0, .width = 0, .fmt = 0, .type_def = NULL }

#define TBL_COLUMNS  .columns = (struct table_column [])
#define TBL_COL_ORDER(order) .column_order = (struct table_col_instance *) order, .cols_to_output = ARRAY_SIZE(order)
#define TBL_COL_DELIMITER(_delimiter) .col_delimiter = _delimiter

/**
 * These macros are used for definition of column order
 **/
#define TBL_COL(_idx) { .idx = _idx, .fmt = XTYPE_FMT_DEFAULT, .next_column = -1 }
#define TBL_COL_FMT(_idx, _fmt) { .idx = _idx, .fmt = _fmt, .next_column = -1 }

/**
 * These macros are aliases to various kinds of table formats.
 **/
#define TBL_FMT_HUMAN_READABLE     .formatter = &table_fmt_human_readable
#define TBL_FMT_BLOCKLINE          .formatter = &table_fmt_blockline
#define TBL_FMT_MACHINE_READABLE   .formatter = &table_fmt_machine_readable

/**
 * The TBL_COL_ITER_START macro are used for iterating over all instances of a particular column in
 * table _tbl.  _colidx is the column index in _tbl, _instptr is the pointer to the column instance
 * (struct table_col_instance *), _idxval is the index of current column index. The variables are
 * enclosed in a block, so they do not introduce variable name collisions.
 *
 * The TBL_COL_ITER_END macro must close the block started with TBL_COL_ITER_START.
 *
 * These macros are usually used to hide the implementation details of the column instances linked
 * list. This is usefull for definition of new types.
 **/
#define TBL_COL_ITER_START(_tbl, _colidx, _instptr, _idxval) { struct table_col_instance *_instptr = NULL; int _idxval = _tbl->ll_headers[_colidx]; \
  for(_idxval = _tbl->ll_headers[_colidx], _instptr = _tbl->column_order + _idxval; _idxval != -1; _idxval = _tbl->column_order[_idxval].next_column, _instptr = _tbl->column_order + _idxval)

#define TBL_COL_ITER_END }

/**
 * Creates a new table from a table template. The template should already contain
 * the definitions of columns.
 **/
struct table *table_init(const struct table_template *tbl_template);

/** Destroy a table instance, freeing all memory used by it. **/
void table_cleanup(struct table *tbl);

/**
 * Start printing of a table. This is a prerequisite to setting of column values.
 * After @table_start() is called, it is no longer possible to change parameters
 * of the table by `table_set_`'something' nor by direct access to the table structure.
 **/
void table_start(struct table *tbl, struct fastbuf *out);

/**
 * This function must be called after all the rows of the current table are printed,
 * making the table structure ready for the next table. You can call `table_set_`'something'
 * between @table_end() and @table_start().
 **/
void table_end(struct table *tbl);

/***
 * Filling tables with data
 * ------------------------
 *
 * For each column type, there are functions for filling of cells
 * of the particular type:
 *
 *   * `table_col_`'type'`(table, idx, value)` sets the cell in column `idx`
 *     to the `value`
 ***/


#define TABLE_COL_PROTO(_name, _type) void table_col_##_name(struct table *tbl, int col, _type val);

TABLE_COL_PROTO(int, int)
TABLE_COL_PROTO(uint, uint)
TABLE_COL_PROTO(double, double)
TABLE_COL_PROTO(intmax, intmax_t)
TABLE_COL_PROTO(uintmax, uintmax_t)
TABLE_COL_PROTO(s64, s64)
TABLE_COL_PROTO(u64, u64)
TABLE_COL_PROTO(bool, bool)

void table_col_str(struct table *tbl, int col, const char * val);

/** TABLE_COL_BODY macro enables easy definitions of bodies of table_col_<something> functions **/
#define TABLE_COL_BODY(_name, _type) void table_col_##_name(struct table *tbl, int col, _type val) {\
    table_col_generic_format(tbl, col, (void*)&val, &xt_##_name);\
  }

/**
 * The table_col_generic_format performs all the checks necessary while filling cell with value,
 * calls the format function from expected_type and stores its result as a cell value. The function
 * guarantees that each column instance is printed with its format.
 **/
void table_col_generic_format(struct table *tbl, int col, void *value, const struct xtype *expected_type);

/**
 * Set a particular cell of the current row to a string formatted
 * by sprintf(). This function can set a column of an arbitrary type.
 **/
void table_col_printf(struct table *tbl, int col, const char *fmt, ...) FORMAT_CHECK(printf, 3, 4);

/**
 * Alternatively, a string cell can be constructed as a stream.
 * This function creates a fastbuf stream connected to the contents
 * of the particular cell. Before you close the stream by @table_col_fbend(),
 * no other operations with cells are allowed.
 **/
struct fastbuf *table_col_fbstart(struct table *tbl, int col);

/**
 * Close the stream that is used for printing of the current column.
 **/
void table_col_fbend(struct table *tbl);

/**
 * Called when all cells of the current row have their values filled in.
 * It sends the completed row to the output stream.
 **/
void table_end_row(struct table *tbl);

/**
 * Resets data in the current row.
 **/
void table_reset_row(struct table *tbl);

/***
 * Configuration functions
 * -----------------------
 ***/

/**
 * Find the index of a column with name @col_name. Returns -1 if there is no such column.
 **/
int table_get_col_idx(struct table *tbl, const char *col_name);


/**
 * Sets a string option to an instance of a columnt type. This is the default version that checks
 * whether the xtype::parse_fmt can be called and calls it. However, there are situation in which
 * the xtype::parse_fmt is not sufficient, e.g., column decoration, post-processing, etc.
 *
 * Each struct table_column has a pointer to a customized version of table_set_col_opt which is
 * called instead of this (default) version of table_set_col_opt
 **/
const char *table_set_col_opt(struct table *tbl, uint col_idx, const char *col_opt);

/**
 * Returns a comma-and-space-separated list of column names, allocated from table's internal
 * memory pool.
 **/
const char *table_get_col_list(struct table *tbl);

/**
 * Sets the order in which the columns are printed. The @col_order parameter is used until
 * @table_end() or @table_cleanup() is called. The table converts the integers in @col_order into
 * internal representation stored in @column_order. Options to column instances can be set using
 * @table_set_col_opt.
 **/
void table_set_col_order(struct table *tbl, int *col_order, int col_order_size);

/**
 * Returns true if col_idx will be printed, false otherwise.
 **/
bool table_col_is_printed(struct table *tbl, uint col_idx);

/**
 * Sets the order in which the columns are printed. The specification is a string with comma-separated column
 * names. Returns NULL for success and an error message otherwise. The string is not referenced after
 * this function returns.
 *
 * The format of the col_order string is the following:
 * <col-order-string> := <col-def>[,<col-def>]*
 *
 * <col-def> := <col-name> '[' <col-opt> ']'
 *
 * <col-name> is a string that does not contain comma ',' or '[',']' brackets
 *
 * <col-opt> is currently only one string without commas. In the future the format can be <str1>,<str2>,... .
 *
 * FIXME In the future, we should allow <col-opt> to be a comma(,) separated list of identifiers
 **/
const char *table_set_col_order_by_name(struct table *tbl, const char *col_order);

/**
 * Sets table formatter. See below for the list of formatters.
 **/
void table_set_formatter(struct table *tbl, const struct table_formatter *fmt);

/**
 * Set a table option. All options have a key and a value. Currently,
 * the following keys are defined (other keys can be accepted by formatters):
 *
 * [options="header"]
 * |===================================================================================================
 * | key	| value				| meaning
 * | `header`	| 0 or 1			| set whether a table header should be printed
 * | `noheader`	| 'none'			| equivalent to `header`=0
 * | `cols`	| comma-separated column list	| set order of columns
 * | `fmt`	| `human`/`machine`/`block`	| set table formatter to one of the built-in formatters
 * | `col-delim`| string			| set column delimiter
 * | `cells`    | string                        | set column format mode
 * |===================================================================================================
 **/
const char *table_set_option_value(struct table *tbl, const char *key, const char *value);

/**
 * Sets a table option given as 'key'`:`'value' or 'key' (with no value).
 **/
const char *table_set_option(struct table *tbl, const char *opt);

/**
 * Sets several table option in 'key'`:`'value' form, stored in a growing array.
 * This is frequently used for options given on the command line.
 **/
const char *table_set_gary_options(struct table *tbl, char **gary_table_opts);

/***
 * Formatters
 * ----------
 *
 * Transformation of abstract cell data to the characters in the output stream
 * is under control of a formatter (which serves as a back-end of the table printer).
 * There are several built-in formatters, but you can define your own.
 *
 * A formatter is described by a structure, which contains pointers to several
 * call-back functions, which are called by the table printer at specific occasions.
 *
 * The formatter can keep its internal state in the `data` field of `struct table`
 * and allocate temporary data from the table's memory pool. Memory allocated in
 * the `row_output` call-back is freed before the next row begins. Memory allocated
 * between the beginning of `table_start` and the end of `table_end` is freed after
 * `table_end`. Memory allocated by `process_option` when no table is started
 * is kept until @table_cleanup().
 ***/

/** Definition of a formatter back-end. **/
struct table_formatter {
  void (*row_output)(struct table *tbl);	// [*] Function that outputs one row
  void (*table_start)(struct table *tbl);	// [*] table_start callback (optional)
  void (*table_end)(struct table *tbl);		// [*] table_end callback (optional)
  bool (*process_option)(struct table *tbl, const char *key, const char *value, const char **err);
	// [*] Process table option and possibly return an error message (optional)
};

/** Standard formatter for human-readable output. **/
extern const struct table_formatter table_fmt_human_readable;

/** Standard formatter for machine-readable output (tab-separated values). **/
extern const struct table_formatter table_fmt_machine_readable;

/**
 * Standard formatter for block output. Each cell is output on its own line
 * of the form `column_name: value`. Rows are separated by blank lines.
 **/
extern const struct table_formatter table_fmt_blockline;

#endif
