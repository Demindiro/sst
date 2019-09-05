#include "optimize/branch.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "func.h"
#include "hashtbl.h"
#include "util.h"


struct branch {
	struct func_line **lines;
	size_t linecount, linecap;
	struct branch *branch0, // Explicit jump
			    *branch1; // Implicit "jump"
	size_t refcount;
	union {
		struct func_line        *l;
		struct func_line_if     *i;
		struct func_line_goto   *g;
		struct func_line_return *r;
	} branchline;
};


/*****
 * Helper functions
 ***/

static void _init_block(struct branch *b)
{
	b->linecount    = 0;
	b->linecap      = 4;
	b->lines        = malloc(b->linecap * sizeof *b->lines);
	if (b->lines == NULL)
		EXITERRNO("Failed to allocate memory", 3);
	b->branch0      = b->branch1 = NULL;
	b->branchline.i = NULL;
	b->refcount     = 0;
}

static void _add_line(struct branch *b, struct func_line *l)
{
	if (b->linecount >= b->linecap) {
		size_t s = b->linecap * 3 / 2;
		struct func_line **t = realloc(b->lines, s * sizeof *b->lines);
		if (t == NULL)
			EXITERRNO("Failed to reallocate memory", 3);
		b->lines   = t;
		b->linecap = s;
	}
	assert(b->linecount < b->linecap);
	b->lines[b->linecount++] = l;
}

static size_t _get_index(struct branch **ba, size_t bac, struct branch *b)
{
	for (size_t i = 0; i < bac; i++) {
		if (ba[i] == b)
			return i;
	}
	assert(!"Nonexistent block");
}

#if 0
#ifndef NDEBUG
static void _print_block_layout(struct branch *b)
{
	char _[256];
	assert(b->linecount != 0);
	for (size_t j = 0; j < b->linecount; j++) {
		switch (b->lines[j]->type) {
		case ASSIGN : _[j] = 'a'; break;
		case DECLARE: _[j] = 'd'; break;
		case DESTROY: _[j] = 'D'; break;
		case MATH   : _[j] = 'm'; break;
		case FUNC   : _[j] = 'f'; break;
		case GOTO   : _[j] = 'G'; break;
		case IF     : _[j] = 'I'; break;
		case LABEL  : _[j] = 'L'; break;
		case LOAD   : _[j] = 'l'; break;
		case RETURN : _[j] = 'R'; break;
		case STORE  : _[j] = 's'; break;
		default: _[j] = '-'; break;
		}
	}
	_[b->linecount] = 0;
	DEBUG("%2lu --> %s", b->refcount, _);
}
#else
#define _print_block_layout(b) NULL
#endif
#endif


/*****
 * Collection of optimizations
 ***/

/**
 * If a jump to a label would result in a jump directly to another label,
 * then the jump to this label can be skipped by going to the goto's destination
 * label directly.
 */
static int _immediate_goto(struct branch **ba, size_t *bac, size_t *i)
{
	struct branch *b = ba[*i],
	              *c = b->branch0;
	if (c != NULL &&
	    c->linecount == 2 &&
	    c->lines[0]->type == LABEL &&
	    c->lines[1]->type == GOTO) {
		const char *lbl = ((struct func_line_goto  *)c->lines[1])->label;
		union func_line_all_p l = { .line = b->branchline.l };
		if (l.line->type == IF) {
			if (streq(l.i->label, lbl))
				return 0;
			l.i->label = lbl;
		} else if (l.line->type == GOTO) {
			if (streq(l.g->label, lbl))
				return 0;
			l.g->label = lbl;
		} else {
			assert(!"Line type is neither IF nor GOTO");
		}
		return 1;
	}
	return 0;
}


/**
 * If a codeblock can only be accessed by one other block, then move the block to in fron
 * of said block and remove the goto and jump label.
 * TODO check if the same optimization can safely be applied to if statements.
 */
static int _one_ref(struct branch **ba, size_t *bac, size_t *i)
{
	struct branch *b = ba[*i],
	                    *c = b->branch0;
	if (c != NULL && c->refcount == 1 &&
	    c->lines[0]->type == LABEL &&
	    b->branchline.g->type == GOTO) {
		// Move blocks
		size_t j = _get_index(ba, *bac, c);
		if (j > *i + 1) {
			memmove(ba + *i + 2, ba + *i + 1, (j - *i - 1) * sizeof *ba);
			ba[*i + 1] = c;
		} else if (j < *i) {
			memmove(ba + j, ba + j + 1, (*i - j - 1) * sizeof *ba);
			ba[*i] = c;
		} else {
			return 0;
		}	
		// Remove label
		/*
		c->linecount--;
		DEBUG("%lu", c->linecount);
		memmove(c->lines, c->lines + 1, c->linecount * sizeof *c->lines);
		*/
		// Remove goto
		b->linecount--;
		if (b->linecount == 0) {
			(*bac)--;
			memmove(ba + *i, ba + *i + 1, (*bac - *i) * sizeof *ba);
			(*i)--;
		}
		return 1;
	}
	return 0;
}


/**
 * If a block is completely inaccessible, remove it
 */
static int _no_ref(struct branch **ba, size_t *bac, size_t *i)
{
	if (ba[*i]->refcount == 0) {
		(*bac)--;
		memmove(ba + *i, ba + *i + 1, (*bac - *i) * sizeof *ba);
		(*i)--;
		return 1;
	}
	return 0;
}


/**
 * Unroll small loops
 * Small means 6 statements (except declare/destroy) or less.
 *
 * The process explained because I keep forgetting it:
 *
 * Say you have this sequence of blocks:
 *
 *     L-----
 *     ----
 *     ---
 *     --G
 *
 * First we make a copy but without the label:
 *
 *     L-----    ====
 *     ----      ===
 *     ---       ==G
 *     --G       =====
 *
 * Note that the copy of the block with the label is at
 * the bottom. It also doesn't have a label statement.
 *
 * branch1 is set to the next copy in each step. The first block
 * (L-----) is linked to the first copy (====) and the last copy
 * (=====) is linked to the second block (----)
 *
 * Next we cut the goto statement from the original block:
 *
 *    L-----    ====
 *    ----      ===
 *    ---       ==G
 *    --        =====
 *
 * We swap the last of the original array with the next-to-last
 * of the copied array:
 *
 *     L-----    ====
 *     ----      ===
 *     ---       --
 *     ==G       =====
 *
 * branch1 is fixed accordingly.
 *
 * We then insert the copies:
 *
 *     L-----
 *     ====
 *     ===
 *     --
 *     =====
 *     ----
 *     ---
 *     ==G
 *
 * Proof of correctness: note that the amount of dashes goes 5-4-3-2.
 * In the final case it goes 5-4-3-2-5-4-3-2
 *
 * Q.E.D. /s
 *
 * Note that it also works with only a single block (aside from swapping
 * and a few linkage steps):
 *
 *     L---G
 *
 *     L---G  ===G
 *
 *     L---   ===G
 *
 *     L---
 *     ===G
 *
 * A few steps have to be done differently though:
 *  - Don't swap the blocks
 *  - Don't link the non-existent second block
 *  - Link the copy to branch1 of the block
 */
static int _unroll(struct branch **ba, size_t *bac, size_t *i)
{
	struct branch *b = ba[*i];
	struct branch *ao[256], *ac[256];
	size_t lc = 0, bc = 0;

	// Find a (small) loop
	struct branch *c = b;
	ao[bc++] = b;
	while (1) {
		// Count the amount of statements
		for (size_t j = c == b; j < c->linecount; j++) {
			if (c->lines[j]->type != DECLARE &&
			    c->lines[j]->type != DESTROY)
				lc++;
			// Too many statements?
			if (lc > 6)
				return 0;
		}
		// Loop! :o
		if (b == c->branch0)
			break;
		// Prevent duplicating labels
		if (c != b && c->lines[0]->type == LABEL)
			return 0;
		// Branch 1 is always an "implicit" jump
		// (the blocks are adjacent)
		if (c->branch1 != NULL)
			c = c->branch1;
		else
			return 0;
		// Add the block to the array
		ao[bc++] = c;
	}

	// Unroll the loop
	// Make copies
	for (size_t j = 0; j < bc; j++) {
		struct branch *copy = malloc(sizeof *copy);
		_init_block(copy);
		for (size_t k = (j == 0); k < ao[j]->linecount; k++) {
			struct func_line *l = ao[j]->lines[k];
			_add_line(copy, copy_line(l));
		}
		size_t k = (j == 0 ? bc - 1 : j - 1);
		ac[k] = copy;
		if (k > 0 && j > 0)
			ac[k - 1]->branch1 = ac[k];
		// Each should be referenced exactly once anyways.
		ac[k]->refcount = 1;
	}

	if (bc == 1) {
		// Link the copy's branch1 to the block's branch1
		ac[0]->branch1 = ao[0]->branch1;
	}

	// Link the first block to the first copy
	ao[0]->branch1 = ac[0];
	if (bc >= 2) {
		// Link the last copy to the second block
		ac[bc - 1]->branch1 = ao[1];
	}

	// Cut the goto statement
	for (size_t j = ao[bc - 1]->linecount - 1; j != -1; j--) {
		if (ao[bc - 1]->branchline.l == ao[bc - 1]->lines[j]) {
			ao[bc - 1]->linecount    = j;
			ao[bc - 1]->branchline.l = NULL;
			ao[bc - 1]->branch0      = NULL;
			break;
		}
	}

	if (bc >= 2) {
		// Swap the last block with the next-to-last copy
		SWAP(struct branch *, ao[bc - 1], ac[bc - 2]);
	}

	// Insert the copies
	size_t j = *i + 1;
	ba[*i + bc - 1] = ao[bc - 1];
	memmove(ba + j + bc, ba + j, (*bac - j) * sizeof *ba);
	memcpy(ba + j, ac, bc * sizeof *ba);
	(*bac) += bc;

	return 1;
}


/*****
 * Main function
 ***/

int optimize_func_branches(func f)
{
	FDEBUG("Applying branch optimization");
	struct branch b[256];
	size_t bc = 0;
	struct hashtbl labels;
	h_create(&labels, 4);

	// Split into blocks
	FDEBUG("Splitting into blocks");
	_init_block(&b[0]);
	for (size_t i = 0; i < f->linecount; i++) {
		union func_line_all_p l = { .line = f->lines[i] };
		switch (l.line->type) {
		default:
			_add_line(&b[bc], l.line);
			break;
		case GOTO:
		case IF:
			_add_line(&b[bc], l.line);
			if (l.line->type == IF)
				b[bc].branchline.i = l.i;
			else
				b[bc].branchline.g = l.g;
			i++;
			while (i < f->linecount) {
				l.line = f->lines[i];
				if (l.line->type == DESTROY)
					_add_line(&b[bc], l.line);
				else
					break;
				i++;
			}
			i--;
			bc++;
			_init_block(&b[bc]);
			break;
		case LABEL:
			if (b[bc].linecount > 0) {
				bc++;
				_init_block(&b[bc]);
			}
			h_add(&labels, l.l->label, bc);
			_add_line(&b[bc], l.line);
			break;
		case RETURN:
			_add_line(&b[bc], l.line);
			b[bc].branchline.r = l.r;
			bc++;
			_init_block(&b[bc]);
			break;
		}
	}
	if (b[bc].linecount > 0)
		bc++;
	// Assume entrypoint is "referenced" at least once
	b[0].refcount++;

	// Link blocks to each other
	FDEBUG("Linking blocks");
	for (size_t i = 0; i < bc; i++) {
		size_t lc = b[i].linecount;
		assert(lc > 0);
		union func_line_all_p l = { .line = b[i].branchline.l };
		size_t bi;
		if (l.line != NULL) {
			switch (l.line->type) {
			case IF:
				if (h_get2(&labels, l.i->label, &bi) < 0) {
					EXIT(1, "Label '%s' doesn't map to any index!",
					        l.i->label);
				}
				assert(i + 1 < bc);
				b[i].branch0 = &b[bi];
				b[i].branch1 = &b[i + 1];
				b[bi   ].refcount++;
				b[i + 1].refcount++;
				break;
			case GOTO:
				if (h_get2(&labels, l.g->label, &bi) < 0) {
					EXIT(1, "Label '%s' doesn't map to any index!",
					        l.g->label);
				}
				b[i].branch0 = &b[bi];
				b[i].branch1 = NULL;
				b[bi].refcount++;
				break;
			case RETURN:
				b[i].branch0 = NULL;
				b[i].branch1 = NULL;
				break;
			default:
				EXIT(1, "Unexpected line type (%d)", l.line->type);
			}
		} else if (i + 1 == bc) {
			b[i].branch0 = NULL;
			b[i].branch1 = NULL;
		} else {
			assert(i + 1 < bc);
			b[i].branch0 = NULL;
			b[i].branch1 = &b[i + 1];
			b[i + 1].refcount++;
		}
	}

	// Optimization time!
	FDEBUG("Optimizing");
	struct branch *bn[2048];
	size_t bnc = 0;
	for (size_t i = 0; i < bc; i++)
		bn[bnc++] = &b[i];
	int changed, haschanged = 0;
	do {
		changed = 0;
		for (size_t i = 0; i < bnc; i++) {
			changed |= _immediate_goto(bn, &bnc, &i);
			changed |= _one_ref(bn, &bnc, &i);
			changed |= _no_ref(bn, &bnc, &i);
			changed |= _unroll(bn, &bnc, &i);
		}
		haschanged |= changed;
	} while (changed);

	// Construct function from new blocks
	FDEBUG("Reconstructing");
	size_t lc = 0;
	for (size_t i = 0; i < bnc; i++)
		lc += bn[i]->linecount;
	struct func_line **fl = realloc(f->lines, lc * sizeof *fl);
	if (fl == NULL)
		EXITERRNO("Failed to reallocate lines array", 3);
	f->lines     = fl;
	f->linecap   = lc;
	f->linecount = lc;
	size_t k = 0;
	for (size_t i = 0; i < bnc; i++) {
		for (size_t j = 0; j < bn[i]->linecount; j++) {
			assert(bn[i]->lines[j] != NULL);
			f->lines[k++] = bn[i]->lines[j];
		}
	}

	return haschanged;
}
