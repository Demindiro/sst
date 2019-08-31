#include "optimize/branch.h"
#include <assert.h>
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

static void _print_block_layout(struct branch *b)
#ifndef NDEBUG
{
	char _[256];
	assert(b->linecount != 0);
	for (size_t j = 0; j < b->linecount; j++) {
		switch (b->lines[j]->type) {
		case FUNC_LINE_ASSIGN : _[j] = 'a'; break;
		case FUNC_LINE_DECLARE: _[j] = 'd'; break;
		case FUNC_LINE_DESTROY: _[j] = 'D'; break;
		case FUNC_LINE_MATH   : _[j] = 'm'; break;
		case FUNC_LINE_FUNC   : _[j] = 'f'; break;
		case FUNC_LINE_GOTO   : _[j] = 'G'; break;
		case FUNC_LINE_IF     : _[j] = 'I'; break;
		case FUNC_LINE_LABEL  : _[j] = 'L'; break;
		case FUNC_LINE_LOAD   : _[j] = 'l'; break;
		case FUNC_LINE_RETURN : _[j] = 'R'; break;
		case FUNC_LINE_STORE  : _[j] = 's'; break;
		default: _[j] = '-'; break;
		}
	}
	_[b->linecount] = 0;
	DEBUG("%2lu --> %s", b->refcount, _);
}
#else
{
}
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
	    c->lines[0]->type == FUNC_LINE_LABEL &&
	    c->lines[1]->type == FUNC_LINE_GOTO) {
		const char *lbl = ((struct func_line_goto  *)c->lines[1])->label;
		DEBUG("IMM GOTO '%s'", lbl);
		union func_line_all_p l = { .line = b->branchline.l };
		if (l.line->type == FUNC_LINE_IF) {
			if (streq(l.i->label, lbl))
				return 0;
			l.i->label = lbl;
		} else if (l.line->type == FUNC_LINE_GOTO) {
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
	    c->lines[0]->type == FUNC_LINE_LABEL &&
	    b->branchline.g->line.type == FUNC_LINE_GOTO) {
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
 */
static int _unroll(struct branch **ba, size_t *bac, size_t *i)
{
	struct branch *b = ba[*i];
	struct branch *a[256];
	size_t lc = 0, bc = 1;

	// Find a (small) loop
	struct branch *c = b;
	while (1) {
		// Count the amount of statements
		for (size_t j = c == b; j < c->linecount; j++) {
			if (c->lines[j]->type != FUNC_LINE_DECLARE &&
			    c->lines[j]->type != FUNC_LINE_DESTROY)
				lc++;
			// Too many statements?
			if (lc > 6)
				return 0;
		}
		// Loop! :o
		if (b == c->branch0)
			break;
		// Prevent duplicating labels
		if (c != b && c->lines[0]->type == FUNC_LINE_LABEL)
			return 0;
		// Branch 1 is always an "implicit" jump
		// (the blocks are adjacent)
		if (c->branch1 != NULL)
			c = c->branch1;
		else
			return 0;
		// Add the block to the array
		a[bc++] = c;
	}

	// Unroll the loop
	// Make a copy of the current block but without a label
	struct branch *copy = malloc(sizeof *copy);
	_init_block(copy);
	for (size_t j = 1; j < b->linecount; j++)
		_add_line(copy, copy_line(b->lines[j]));
	// This copy has no label, so refcount is 1
	copy->refcount = 1;
	// Replace the duplicate with the copy
	a[0] = copy;

	// Copy the other blocks
	struct branch *last = a[bc - 1];
	for (size_t j = 1; j < bc; j++) {
		struct branch *copy = malloc(sizeof *copy);
		_init_block(copy);
		size_t l = a[j]->linecount;
		// If it is the last block, exclude the goto statement
		// If it is not a goto statement (an if statement), keep it
		// If there are any destroy statements after the goto, exclude
		// those too
		size_t excl = -1;
		if (j + 1 == bc) {
			for (ssize_t k = l - 1; k >= 0; k--) {
				if (a[j]->lines[k]->type == FUNC_LINE_GOTO) {
					excl = k;
					break;
				}
			}
		}
		for (size_t k = 0; k < l; k++) {
			if (k >= excl)
				continue;
			struct func_line *fl = a[j]->lines[k];
			_add_line(copy, copy_line(fl));
		}
		// The copies have no labels, so refcount is always 1
		copy->refcount = 1;
		a[j] = copy;
		a[j - 1]->branch1 = a[j];
	}
	struct branch *last_branch1 = last->branch1;
	// Swap the last found block with the last block of the array
	SWAP(struct branch *, last, a[bc - 1]);
	// Correct linkage
	b->branch1 = last;
	last->branch1 = a[0];
	a[bc - 1]->branch1 = last_branch1;

	size_t j = _get_index(ba, *bac, c) + 1;
	// Reserve 'bc' slots
	memmove(ba + j + bc, ba + j, (*bac - j) * sizeof *ba);
	// Insert the array after the position of the last found block
	memcpy(ba + j, a, bc * sizeof *ba);
	(*bac) += bc;
	// Set the last block of the array in the slot of the last found block
	ba[j - 1] = last;

	for (size_t i = 0; i < *bac; i++)
		_print_block_layout(ba[i]);

	return 1;
}


/*****
 * Main function
 ***/

int optimize_func_branches(func f)
{
#define FDEBUG(m, ...) DEBUG("[%s] " m, f->name, ##__VA_ARGS__)
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
		case FUNC_LINE_GOTO:
		case FUNC_LINE_IF:
			_add_line(&b[bc], l.line);
			if (l.line->type == FUNC_LINE_IF)
				b[bc].branchline.i = l.i;
			else
				b[bc].branchline.g = l.g;
			i++;
			while (i < f->linecount) {
				l.line = f->lines[i];
				if (l.line->type == FUNC_LINE_DESTROY)
					_add_line(&b[bc], l.line);
				else
					break;
				i++;
			}
			i--;
			bc++;
			_init_block(&b[bc]);
			break;
		case FUNC_LINE_LABEL:
			if (b[bc].linecount > 0) {
				bc++;
				_init_block(&b[bc]);
			}
			h_add(&labels, l.l->label, bc);
			_add_line(&b[bc], l.line);
			break;
		case FUNC_LINE_RETURN:
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
			case FUNC_LINE_IF:
				if (h_get2(&labels, l.i->label, &bi) < 0) {
					ERROR("Label '%s' doesn't map to any index!",
					      l.i->label);
					EXIT(1);
				}
				assert(i + 1 < bc);
				b[i].branch0 = &b[bi];
				b[i].branch1 = &b[i + 1];
				b[bi   ].refcount++;
				b[i + 1].refcount++;
				break;
			case FUNC_LINE_GOTO:
				if (h_get2(&labels, l.g->label, &bi) < 0) {
					ERROR("Label '%s' doesn't map to any index!",
					      l.g->label);
					EXIT(1);
				}
				b[i].branch0 = &b[bi];
				b[i].branch1 = NULL;
				b[bi].refcount++;
				break;
			case FUNC_LINE_RETURN:
				b[i].branch0 = NULL;
				b[i].branch1 = NULL;
				break;
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


	for (size_t i = 0; i < bc; i++)
		_print_block_layout(&b[i]);


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
		//sleep(2);
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
