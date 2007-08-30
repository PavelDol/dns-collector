/*
 *	UCW Library -- Universal Sorter: Multi-Way Merge Module
 *
 *	(c) 2007 Martin Mares <mj@ucw.cz>
 *
 *	This software may be freely distributed and used according to the terms
 *	of the GNU Lesser General Public License.
 */

typedef struct P(mwt_node) {
  int i;
#ifdef SORT_UNIFY
  int eq;
#endif
} P(mwt_node);

static inline void P(update_tree)(P(key) *keys, P(mwt_node) *tree, uns i)
{
  while (i /= 2)
    {
      if (tree[2*i].i < 0)
	tree[i] = tree[2*i+1];
      else if (tree[2*i+1].i < 0)
	tree[i] = tree[2*i];
      else
	{
	  int cmp = P(compare)(&keys[tree[2*i].i], &keys[tree[2*i+1].i]);
	  tree[i] = (cmp <= 0) ? tree[2*i] : tree[2*i+1];
#ifdef SORT_UNIFY
	  if (!cmp)
	    tree[i].eq = 1;
#endif
	}
      /*
       * It is very tempting to stop as soon as the current node does not
       * change, but it is wrong, because even if the stream index stored in
       * the tree is the same, the actual key value can differ.
       */
    }
#if defined(__GNUC__) && (__GNUC__ * 1000 + __GNUC_MINOR__ < 4002)
  /*
   * This function sometimes triggers optimizer bugs in GCC 4.1.1 and
   * possibly also earlier versions, leading to an assumption that tree[1]
   * does not change during this function. We add an explicit memory barrier
   * as a work-around. Ugh.
   */
  asm volatile ("" : : : "memory");
#endif
}

static inline void P(set_tree)(P(key) *keys, P(mwt_node) *tree, uns i, int val)
{
  tree[i].i = val;
  P(update_tree)(keys, tree, i);
}

static void P(multiway_merge)(struct sort_context *ctx UNUSED, struct sort_bucket **ins, struct sort_bucket *out)
{
  uns num_ins = 0;
  while (ins[num_ins])
    num_ins++;

  uns n2 = 1;
  while (n2 < num_ins)
    n2 *= 2;

  struct fastbuf *fout = sbuck_write(out);
  struct fastbuf *fins[num_ins];
  P(key) keys[num_ins];
  P(mwt_node) tree[2*n2];		// A complete binary tree, leaves are input streams, each internal vertex contains a minimum of its sons
  for (uns i=1; i<2*n2; i++)
    tree[i] = (P(mwt_node)) { .i = -1 };

  for (uns i=0; i<num_ins; i++)
    {
      fins[i] = sbuck_read(ins[i]);
      if (P(read_key)(fins[i], &keys[i]))
	P(set_tree)(keys, tree, n2+i, i);
    }

#ifdef SORT_UNIFY

  uns hits[num_ins];
  P(key) *mkeys[num_ins], *key;
  struct fastbuf *mfb[num_ins];

  while (likely(tree[1].i >= 0))
    {
      int i = tree[1].i;
      if (!tree[1].eq)
	{
	  /* The key is unique, so let's go through the fast path */
	  P(copy_data)(&keys[i], fins[i], fout);
	  if (unlikely(!P(read_key)(fins[i], &keys[i])))
	    tree[n2+i].i = -1;
	  P(update_tree)(keys, tree, n2+i);
	  continue;
	}

      uns m = 0;
      key = &keys[i];
      do
	{
	  hits[m] = i;
	  mkeys[m] = &keys[i];
	  mfb[m] = fins[i];
	  m++;
	  P(set_tree)(keys, tree, n2+i, -1);
	  i = tree[1].i;
	  if (unlikely(i < 0))
	    break;
	}
      while (!P(compare)(key, &keys[i]));

      P(copy_merged)(mkeys, mfb, m, fout);

      for (uns j=0; j<m; j++)
	{
	  i = hits[j];
	  if (likely(P(read_key)(fins[i], &keys[i])))
	    P(set_tree)(keys, tree, n2+i, i);
	}
    }

#else

  /* Simplified version which does not support any unification */
  while (likely(tree[1].i >= 0))
    {
      uns i = tree[1].i;
      P(key) UNUSED key = keys[i];
      P(copy_data)(&keys[i], fins[i], fout);
      if (unlikely(!P(read_key)(fins[i], &keys[i])))
	tree[n2+i].i = -1;
      P(update_tree)(keys, tree, n2+i);
#ifdef SORT_ASSERT_UNIQUE
      ASSERT(tree[1].i < 0 || P(compare)(&key, &keys[tree[1].i]) < 0);
#endif
    }

#endif

  out->runs++;
}
