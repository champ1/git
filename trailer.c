#include "cache.h"
/*
 * Copyright (c) 2013 Christian Couder <chriscool@tuxfamily.org>
 */

enum action_where { WHERE_AFTER, WHERE_BEFORE };
enum action_if_exist { EXIST_ADD_IF_DIFFERENT, EXIST_ADD_IF_DIFFERENT_NEIGHBOR,
		       EXIST_ADD, EXIST_OVERWRITE, EXIST_DO_NOTHING };
enum action_if_missing { MISSING_ADD, MISSING_DO_NOTHING };

struct conf_info {
	char *name;
	char *key;
	char *command;
	enum action_where where;
	enum action_if_exist if_exist;
	enum action_if_missing if_missing;
};

struct trailer_item {
	struct trailer_item *previous;
	struct trailer_item *next;
	const char *token;
	const char *value;
	struct conf_info *conf;
};

static int same_token(struct trailer_item *a, struct trailer_item *b, int alnum_len)
{
	return !strncasecmp(a->token, b->token, alnum_len);
}

static int same_value(struct trailer_item *a, struct trailer_item *b)
{
	return !strcasecmp(a->value, b->value);
}

static int same_trailer(struct trailer_item *a, struct trailer_item *b, int alnum_len)
{
	return same_token(a, b, alnum_len) && same_value(a, b);
}

/* Get the length of buf from its beginning until its last alphanumeric character */
static size_t alnum_len(const char *buf, size_t len) {
	while (--len >= 0 && !isalnum(buf[len]));
	return len + 1;
}

static void add_arg_to_infile(struct trailer_item *infile_tok,
			      struct trailer_item *arg_tok)
{
	if (arg_tok->conf->where == WHERE_AFTER) {
		arg_tok->next = infile_tok->next;
		infile_tok->next = arg_tok;
		arg_tok->previous = infile_tok;
		if (arg_tok->next)
			arg_tok->next->previous = arg_tok;
	} else {
		arg_tok->previous = infile_tok->previous;
		infile_tok->previous = arg_tok;
		arg_tok->next = infile_tok;
		if (arg_tok->previous)
			arg_tok->previous->next = arg_tok;
	}
}

static int check_if_different(struct trailer_item *infile_tok,
			      struct trailer_item *arg_tok,
			      int alnum_len, int check_all)
{
	enum action_where where = arg_tok->conf->where;
	do {
		if (!infile_tok)
			return 1;
		if (same_trailer(infile_tok, arg_tok, alnum_len))
			return 0;
		/*
		 * if we want to add a trailer after another one,
		 * we have to check those before this one
		 */
		infile_tok = (where == WHERE_AFTER) ? infile_tok->previous : infile_tok->next;
	} while (check_all);
	return 1;
}

static void apply_arg_if_exist(struct trailer_item *infile_tok,
			       struct trailer_item *arg_tok,
			       int alnum_len)
{
	switch(arg_tok->conf->if_exist) {
	case EXIST_DO_NOTHING:
		free(arg_tok);
		break;
	case EXIST_OVERWRITE:
		free((char *)infile_tok->value);
		infile_tok->value = xstrdup(arg_tok->value);
		free(arg_tok);
		break;
	case EXIST_ADD:
		add_arg_to_infile(infile_tok, arg_tok);
		break;
	case EXIST_ADD_IF_DIFFERENT:
		if (check_if_different(infile_tok, arg_tok, alnum_len, 1))
			add_arg_to_infile(infile_tok, arg_tok);
		else
			free(arg_tok);
		break;
	case EXIST_ADD_IF_DIFFERENT_NEIGHBOR:
		if (check_if_different(infile_tok, arg_tok, alnum_len, 0))
			add_arg_to_infile(infile_tok, arg_tok);
		else
			free(arg_tok);
		break;
	}
}

static void remove_from_list(struct trailer_item *item,
			     struct trailer_item **first)
{
	if (item->next)
		item->next->previous = item->previous;
	if (item->previous)
		item->previous->next = item->next;
	else
		*first = item->next;
}

static struct trailer_item *remove_first(struct trailer_item **first)
{
	struct trailer_item *item = *first;
	*first = item->next;
	if (item->next) {
		item->next->previous = NULL;
		item->next = NULL;
	}
	return item;
}

static void process_infile_tok(struct trailer_item *infile_tok,
			       struct trailer_item **arg_tok_first,
			       enum action_where where)
{
	struct trailer_item *arg_tok;
	struct trailer_item *next_arg;

	int tok_alnum_len = alnum_len(infile_tok->token, strlen(infile_tok->token));
	for (arg_tok = *arg_tok_first; arg_tok; arg_tok = next_arg) {
		next_arg = arg_tok->next;
		if (same_token(infile_tok, arg_tok, tok_alnum_len) &&
		    arg_tok->conf->where == where) {
			/* Remove arg_tok from list */
			remove_from_list(arg_tok, arg_tok_first);
			/* Apply arg */
			apply_arg_if_exist(infile_tok, arg_tok, tok_alnum_len);
			/*
			 * If arg has been added to infile,
			 * then we need to process it too now.
			 */
			if ((where == WHERE_AFTER ? infile_tok->next : infile_tok->previous) == arg_tok)
				infile_tok = arg_tok;
		}
	}
}

static void update_last(struct trailer_item **last)
{
	if (*last)
		while((*last)->next != NULL)
			*last = (*last)->next;
}

static void update_first(struct trailer_item **first)
{
	if (*first)
		while((*first)->previous != NULL)
			*first = (*first)->previous;
}

static void apply_arg_if_missing(struct trailer_item **infile_tok_first,
				 struct trailer_item **infile_tok_last,
				 struct trailer_item *arg_tok)
{
	struct trailer_item **infile_tok;
	enum action_where where;

	switch(arg_tok->conf->if_missing) {
	case MISSING_DO_NOTHING:
		free(arg_tok);
		break;
	case MISSING_ADD:
		where = arg_tok->conf->where;
		infile_tok = (where == WHERE_AFTER) ? infile_tok_last : infile_tok_first;
		if (*infile_tok) {
			add_arg_to_infile(*infile_tok, arg_tok);
			*infile_tok = arg_tok;
		} else {
			*infile_tok_first = arg_tok;
			*infile_tok_last = arg_tok;
		}
		break;
	}
}

static void process_trailers_lists(struct trailer_item **infile_tok_first,
				   struct trailer_item **infile_tok_last,
				   struct trailer_item **arg_tok_first)
{
	struct trailer_item *infile_tok;
	struct trailer_item *arg_tok;

	if (!*arg_tok_first)
		return;

	/* Process infile from end to start */
	for (infile_tok = *infile_tok_last; infile_tok; infile_tok = infile_tok->previous) {
		process_infile_tok(infile_tok, arg_tok_first, WHERE_AFTER);
	}

	update_last(infile_tok_last);

	if (!*arg_tok_first)
		return;

	/* Process infile from start to end */
	for (infile_tok = *infile_tok_first; infile_tok; infile_tok = infile_tok->next) {
		process_infile_tok(infile_tok, arg_tok_first, WHERE_BEFORE);
	}

	update_first(infile_tok_first);

	/* Process args left */
	while (*arg_tok_first) {
		arg_tok = remove_first(arg_tok_first);
		apply_arg_if_missing(infile_tok_first, infile_tok_last, arg_tok);
	}
}
