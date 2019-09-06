#include "optimize/dependency.h"
#include "func.h"
#include "hashtbl.h"
#include "types.h"
#include "util.h"


// Prime fast: 34 --> 

int optimize_dependency(func f, struct hashtbl *nodepends)
{
	FDEBUG("Applying dependency optimization");
	struct hashtbl depends, deref_depends;
	h_create(&depends, 8);
	h_create(&deref_depends, 8);

	// Find all variables that may have a direct external influences
	for (size_t i = 0; i < f->linecount; i++) {
		union func_line_all_p l = { .line = f->lines[i] };
		switch (l.line->type) {
		case ASM:
			// Assembly may do all kinds of weird stuff, so assume that all
			// input variables have an external influence
			for (size_t j = 0; j < l.as->incount; j++)
				h_add(&depends, l.as->invars[j], 0);
			break;
		case FUNC:
			for (size_t j = 0; j < l.f->argcount; j++)
				h_add(&depends, l.f->args[j], 0);
			break;
		case RETURN:
			h_add(&depends, l.r->val, 0);
			break;
		case ASSIGN:
		case DECLARE:
		case DESTROY:
		case GOTO:
		case IF:
		case LABEL:
		case MATH:
		case STORE:
			break;
		default:
			EXIT(3, "Unknown line type (%d)", l.line->type);
		}
	}
	// Arrays and pointers as arguments may have external influences if assigned to
	for (size_t i = 0; i < f->argcount; i++) {
		struct type t;
		if (get_type(&t, f->args[i].type) < 0)
			abort();
		if (t.type == TYPE_ARRAY || t.type == TYPE_CLASS)
			h_add(&deref_depends, f->args[i].name, 0);
	}

	// Find all variables that may have indirect external influences
	int newdepend;
	do {
		newdepend = 0;
#define ADDDEPEND(x) do {		\
	if (h_get(&depends, x) == -1) {	\
		h_add(&depends, x, 0);	\
		newdepend = 1;		\
	}				\
} while (0)
		for (size_t i = 0; i < f->linecount; i++) {
			union func_line_all_p l = { .line = f->lines[i] };
			switch (l.line->type) {
			case ASSIGN:
				if (h_get(&depends, l.a->var) == 0)
					ADDDEPEND(l.a->value);
				break;
			case MATH:
				if (h_get(&depends, l.m->x) == 0) {
					ADDDEPEND(l.m->y);
					if (l.m->z != NULL)
						ADDDEPEND(l.m->z);
				}
			case STORE:
				if (h_get(&deref_depends, l.s->var) == 0) {
					ADDDEPEND(l.s->val  );
					ADDDEPEND(l.s->index);
				}
				break;
			case IF: {
				if (h_get(&depends, l.i->var) == 0)
					break;
				size_t j;
				for (j = 0; j < f->linecount; j++) {
					union func_line_all_p l2 = { .line = f->lines[j] };
					if (l2.line->type == LABEL &&
					    streq(l2.l->label, l.i->label))
					    goto found;
				}
				EXIT(3, "Label '%s' doesn't exist", l.i->label);
			found:
				; size_t start = i, stop = j;
				if (start > stop)
					SWAP(size_t, start, stop);
				for (size_t k = start; k < stop; k++) {
					union func_line_all_p l2 = { .line = f->lines[k] };
					switch (l2.line->type) {
						case ASSIGN:
							if (h_get(&depends, l.a->var) == 0)
								goto depends;
							break;
						case FUNC:
							goto depends; // A function can have external influences
						case MATH:
							if (h_get(&depends, l.m->x) == 0)
								goto depends;
							break;
						case STORE:
							if (h_get(&deref_depends, l.s->var) == 0)
								goto depends;
							break;
						case RETURN:
							goto depends; // It exits the function completely, potentially
								      // affecting all variables
						case DECLARE:
						case DESTROY:
						case GOTO:
						case IF: // It *can* have influence, but it's more complicated
						case LABEL:
							break;
						default:
							EXIT(3, "Unknown line type (%d)", l.line->type);
					}
				}
				goto nodepends;
			depends:
				h_add(&depends, l.i->var, 0);
			nodepends:;
			}
				break;
			case DECLARE:
			case DESTROY:
			case FUNC:
			case GOTO:
			case LABEL:
			case RETURN:
				break;
			default:
				EXIT(3, "Unknown line type (%d)", l.line->type);
			}
		}
	} while (newdepend);

	// Eliminate variables that aren't dependees
	int _nodepends = 0;
	for (size_t i = 0; i < f->linecount; i++) {
#define NODEPENDS(x) do {		\
	if (!_nodepends)		\
		h_create(nodepends, 4);	\
	h_add(nodepends, x, 0);		\
	_nodepends = 1;			\
} while (0)
		union func_line_all_p l = { .line = f->lines[i] };
		switch (l.line->type) {
		case ASSIGN:
			if (h_get(&depends, l.a->var) == -1)
				NODEPENDS(l.a->var);
			break;
		case DECLARE:
			if (h_get(&depends, l.d->var) == -1)
				NODEPENDS(l.d->var);
			break;
		case DESTROY:
			if (h_get(&depends, l.d->var) == -1)
				NODEPENDS(l.d->var);
			break;
		case FUNC:
			if (h_get(&depends, l.f->var) == -1)
				NODEPENDS(l.f->var);
			break;
		case IF:
			if (h_get(&depends, l.i->var) == -1)
				NODEPENDS(l.i->var);
			break;
		case MATH:
			if (h_get(&depends, l.m->x) == -1)
				NODEPENDS(l.m->x);
			break;
		case STORE:
			// TODO
			break;
		case LABEL:
		case GOTO:
		case RETURN:
			break;
		default:
			EXIT(3, "Unknown line type (%d)", l.line->type);
		}
	}

	return _nodepends;
}
