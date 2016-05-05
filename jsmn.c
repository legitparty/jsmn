#include "jsmn.h"
#ifdef JSMN_DOM
#include <string.h> /* for memcpy() */
#endif
/**
 * Allocates a fresh unused token from the token pull.
 */
static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,
		jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *tok;
	if (parser->toknext >= num_tokens) {
		return NULL;
	}
	tok = &tokens[parser->toknext++];
	tok->start = tok->end = -1;
#ifdef JSMN_DOM
	tok->family.parent = -1;
	tok->family.siblings.prev = -1;
	tok->family.siblings.next = -1;
	tok->family.children.first = -1;
	tok->family.children.last = -1;
#else
	tok->size = 0;
#ifdef JSMN_PARENT_LINKS
	tok->parent = -1;
#endif
#endif
	return tok;
}

/**
 * Fills token type and boundaries.
 */
static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type,
                            int start, int end) {
	token->type = type;
	token->start = start;
	token->end = end;
#ifdef JSMN_DOM
#else
	token->size = 0;
#endif
}

/**
 * Fills next available token with JSON primitive.
 */
static int jsmn_parse_primitive(jsmn_parser *parser, const char *js,
		size_t len, jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *token;
	int start;
#ifdef JSMN_DOM
	int dom_i;
#endif

	start = parser->pos;

	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		switch (js[parser->pos]) {
#ifndef JSMN_STRICT
			/* In strict mode primitive must be followed by "," or "}" or "]" */
			case ':':
#endif
			case '\t' : case '\r' : case '\n' : case ' ' :
			case ','  : case ']'  : case '}' :
				goto found;
		}
		if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
			parser->pos = start;
			return JSMN_ERROR_INVAL;
		}
	}
#ifdef JSMN_STRICT
	/* In strict mode primitive must be followed by a comma/object/array */
	parser->pos = start;
	return JSMN_ERROR_PART;
#endif

found:
	if (tokens == NULL) {
		parser->pos--;
		return 0;
	}
#ifdef JSMN_DOM
	dom_i = jsmn_dom_new_as(parser, tokens, num_tokens, JSMN_PRIMITIVE, start, parser->pos);
	if (dom_i < 0) {
		parser->pos = start;
		return dom_i;
	}
	dom_i = jsmn_dom_add(parser, tokens, num_tokens, parser->toksuper, dom_i);
	if (dom_i < 0) {
		parser->pos = start;
		return dom_i;
	}
#else
	token = jsmn_alloc_token(parser, tokens, num_tokens);
	if (token == NULL) {
		parser->pos = start;
		return JSMN_ERROR_NOMEM;
	}
	jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
#ifdef JSMN_PARENT_LINKS
	token->parent = parser->toksuper;
#endif
#endif
	parser->pos--;
	return 0;
}

/**
 * Fills next token with JSON string.
 */
static int jsmn_parse_string(jsmn_parser *parser, const char *js,
		size_t len, jsmntok_t *tokens, size_t num_tokens) {
#ifdef JSMN_DOM
	int dom_i;
#else
	jsmntok_t *token;
#endif

	int start = parser->pos;

	parser->pos++;

	/* Skip starting quote */
	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		char c = js[parser->pos];

		/* Quote: end of string */
		if (c == '\"') {
			if (tokens == NULL) {
				return 0;
			}
#ifdef JSMN_DOM
			dom_i = jsmn_dom_new_as(parser, tokens, num_tokens, JSMN_STRING, start + 1, parser->pos);
			if (dom_i < 0) {
				parser->pos = start;
				return dom_i;
			}
			dom_i = jsmn_dom_add(parser, tokens, num_tokens, parser->toksuper, dom_i);
			if (dom_i < 0) {
				parser->pos = start;
				return dom_i;
			}
#else
			token = jsmn_alloc_token(parser, tokens, num_tokens);
			if (token == NULL) {
				parser->pos = start;
				return JSMN_ERROR_NOMEM;
			}
			jsmn_fill_token(token, JSMN_STRING, start+1, parser->pos);
#ifdef JSMN_PARENT_LINKS
			token->parent = parser->toksuper;
#endif
#endif
			return 0;
		}

		/* Backslash: Quoted symbol expected */
		if (c == '\\' && parser->pos + 1 < len) {
			int i;
			parser->pos++;
			switch (js[parser->pos]) {
				/* Allowed escaped symbols */
				case '\"': case '/' : case '\\' : case 'b' :
				case 'f' : case 'r' : case 'n'  : case 't' :
					break;
				/* Allows escaped symbol \uXXXX */
				case 'u':
					parser->pos++;
					for(i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++) {
						/* If it isn't a hex character we have an error */
						if(!((js[parser->pos] >= 48 && js[parser->pos] <= 57) || /* 0-9 */
									(js[parser->pos] >= 65 && js[parser->pos] <= 70) || /* A-F */
									(js[parser->pos] >= 97 && js[parser->pos] <= 102))) { /* a-f */
							parser->pos = start;
							return JSMN_ERROR_INVAL;
						}
						parser->pos++;
					}
					parser->pos--;
					break;
				/* Unexpected symbol */
				default:
					parser->pos = start;
					return JSMN_ERROR_INVAL;
			}
		}
	}
	parser->pos = start;
	return JSMN_ERROR_PART;
}

/**
 * Parse JSON string and fill tokens.
 */
int jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
		jsmntok_t *tokens, unsigned int num_tokens) {
	int r;
	int i;
	jsmntok_t *token;
	int count = parser->toknext;
#ifdef JSMN_DOM
	int dom_i;
#endif

	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		char c;
		jsmntype_t type;

		c = js[parser->pos];
		switch (c) {
			case '{': case '[':
				count++;
				if (tokens == NULL) {
					break;
				}
#ifdef JSMN_DOM
				dom_i = jsmn_dom_new_as(parser, tokens, num_tokens, c == '{' ? JSMN_OBJECT : JSMN_ARRAY, parser->pos, -1);
				if (dom_i < 0) {
					return dom_i;
				}
				dom_i = jsmn_dom_add(parser, tokens, num_tokens, parser->toksuper, dom_i);
				if (dom_i < 0) {
					return dom_i;
				}
				parser->toksuper = dom_i;
#else
				token = jsmn_alloc_token(parser, tokens, num_tokens);
				if (token == NULL)
					return JSMN_ERROR_NOMEM;
				if (parser->toksuper != -1) {
					tokens[parser->toksuper].size++;
#ifdef JSMN_PARENT_LINKS
					token->parent = parser->toksuper;
#endif
				}
				token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
				token->start = parser->pos;
				parser->toksuper = parser->toknext - 1;
#endif
				break;
			case '}': case ']':
				if (tokens == NULL)
					break;
				type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
#ifdef JSMN_DOM
				if (parser->toknext < 1) {
					return JSMN_ERROR_INVAL;
				}
				dom_i = parser->toknext - 1;
				for (;;) {
					if (jsmn_dom_is_open(parser, tokens, num_tokens, dom_i)) {
						if (jsmn_dom_get_type(parser, tokens, num_tokens, dom_i) != type) {
							return JSMN_ERROR_INVAL;
						}
						jsmn_dom_close(parser, tokens, num_tokens, dom_i, parser->pos + 1);
						parser->toksuper = jsmn_dom_get_parent(parser, tokens, num_tokens, dom_i);
						break;
					}
					if (jsmn_dom_get_parent(parser, tokens, num_tokens, dom_i) == -1) {
						break;
					}
					dom_i = jsmn_dom_get_parent(parser, tokens, num_tokens, dom_i);
				}
			
#else
				/* Open tokens have a set `start`, but an unset (-1) `end`. */
#ifdef JSMN_PARENT_LINKS
				/* Find container's explicit open `parent` token. Set its `end`. Set `toksuper` to its explicit `parent`. */
				if (parser->toknext < 1) {
					return JSMN_ERROR_INVAL;
				}
				token = &tokens[parser->toknext - 1];
				for (;;) {
					if (token->start != -1 && token->end == -1) {
						if (token->type != type) {
							return JSMN_ERROR_INVAL;
						}
						token->end = parser->pos + 1;
						parser->toksuper = token->parent;
						break;
					}
					if (token->parent == -1) {
						break;
					}
					token = &tokens[token->parent];
				}
#else
				/* Find container's latest open token. Set its `end`. */
				for (i = parser->toknext - 1; i >= 0; i--) {
					token = &tokens[i];
					if (token->start != -1 && token->end == -1) {
						if (token->type != type) {
							return JSMN_ERROR_INVAL;
						}
						parser->toksuper = -1;
						token->end = parser->pos + 1;
						break;
					}
				}
				/* Error if unmatched closing bracket */
				if (i == -1) return JSMN_ERROR_INVAL;
				/* Find the next latest open token. Set that to the current `toksuper`. */
				for (; i >= 0; i--) {
					token = &tokens[i];
					if (token->start != -1 && token->end == -1) {
						parser->toksuper = i;
						break;
					}
				}
#endif
#endif
				break;
			case '\"':
				r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
				if (r < 0) return r;
				count++;
#ifdef JSMN_DOM
#else
				if (parser->toksuper != -1 && tokens != NULL)
					tokens[parser->toksuper].size++;
#endif
				break;
			case '\t' : case '\r' : case '\n' : case ' ':
				break;
			case ':':
				parser->toksuper = parser->toknext - 1;
				break;
			case ',':
				if (tokens != NULL && parser->toksuper != -1 &&
						tokens[parser->toksuper].type != JSMN_ARRAY &&
						tokens[parser->toksuper].type != JSMN_OBJECT) {
#ifdef JSMN_DOM
					dom_i = jsmn_dom_get_parent(parser, tokens, num_tokens, parser->toksuper);
					if (dom_i < 0) {
						return dom_i;
					}
					parser->toksuper = dom_i;
#else
#ifdef JSMN_PARENT_LINKS
					parser->toksuper = tokens[parser->toksuper].parent;
#else
					for (i = parser->toknext - 1; i >= 0; i--) {
						if (tokens[i].type == JSMN_ARRAY || tokens[i].type == JSMN_OBJECT) {
							if (tokens[i].start != -1 && tokens[i].end == -1) {
								parser->toksuper = i;
								break;
							}
						}
					}
#endif
#endif
				}
				break;
#ifdef JSMN_STRICT
			/* In strict mode primitives are: numbers and booleans */
			case '-': case '0': case '1' : case '2': case '3' : case '4':
			case '5': case '6': case '7' : case '8': case '9':
			case 't': case 'f': case 'n' :
				/* And they must not be keys of the object */
				if (tokens != NULL && parser->toksuper != -1) {
					jsmntok_t *t = &tokens[parser->toksuper];
					if (t->type == JSMN_OBJECT ||
							(t->type == JSMN_STRING && t->size != 0)) {
						return JSMN_ERROR_INVAL;
					}
				}
#else
			/* In non-strict mode every unquoted value is a primitive */
			default:
#endif
				r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
				if (r < 0) return r;
				count++;
#ifdef JSMN_DOM
#else
				if (parser->toksuper != -1 && tokens != NULL)
					tokens[parser->toksuper].size++;
#endif
				break;

#ifdef JSMN_STRICT
			/* Unexpected char in strict mode */
			default:
				return JSMN_ERROR_INVAL;
#endif
		}
	}

	if (tokens != NULL) {
		for (i = parser->toknext - 1; i >= 0; i--) {
			/* Unmatched opened object or array */
			if (tokens[i].start != -1 && tokens[i].end == -1) {
				return JSMN_ERROR_PART;
			}
		}
	}

	return count;
}

/**
 * Creates a new parser based over a given  buffer with an array of tokens
 * available.
 */
void jsmn_init(jsmn_parser *parser) {
	parser->pos = 0;
	parser->toknext = 0;
	parser->toksuper = -1;
#ifdef JSMN_DOM
#endif
}

#ifdef JSMN_DOM
int jsmn_dom_get_value(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens, int i, char *buf, size_t buflen) {
	size_t size;
	size_t min_size;

	if (i >= (int) num_tokens || tokens[i].end < tokens[i].start || buflen < 1) {
		return JSMN_ERROR_INVAL;
	}

	size = tokens[i].end - tokens[i].start;
	min_size = size < buflen - 1 ? size : buflen - 1;

	memcpy(buf, &js[tokens[i].start], min_size);
	buf[min_size] = '\0';

	return size;
}
size_t jsmn_dom_get_strlen(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i) {
	if (i >= (int) num_tokens) {
		return 0;
	}

	if (tokens[i].end >= tokens[i].start) {
		return tokens[i].end - tokens[i].start;
	} else {
		return 0;
	}
}
size_t jsmn_dom_get_count(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i) {
	size_t count;
	int dom_i;

	if (i >= (int) num_tokens) {
		return 0;
	}

	count = 0;

	dom_i = jsmn_dom_get_child(parser, tokens, num_tokens, i);
	while (dom_i != -1) {
		count++;
		dom_i = jsmn_dom_get_sibling(parser, tokens, num_tokens, dom_i);
	}

	return count;
}
int jsmn_dom_get_type(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i) {
	if (i >= (int) num_tokens) {
		return -1;
	}

	return tokens[i].type;
}
int jsmn_dom_get_parent(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i) {
	if (i >= (int) num_tokens) {
		return -1;
	}

	return tokens[i].family.parent;
}
int jsmn_dom_get_child(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i) {
	if (i >= (int) num_tokens) {
		return -1;
	}

	return tokens[i].family.children.first;
}
int jsmn_dom_get_sibling(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i) {
	if (i >= (int) num_tokens) {
		return -1;
	}

	return tokens[i].family.siblings.next;
}
int jsmn_dom_is_open(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i) {
	if (i >= (int) num_tokens) {
		return 0;
	}

	return tokens[i].start != -1 && tokens[i].end == -1;
}
int jsmn_dom_add(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int parent_i, int i) {
	if (i >= (int) num_tokens || parent_i >= (int) num_tokens) {
		return JSMN_ERROR_INVAL;
	}

	tokens[i].family.parent = parent_i;
	if (parent_i != -1) {
		if (tokens[parent_i].family.children.first == -1) {
			tokens[parent_i].family.children.first = i;
			tokens[parent_i].family.children.last  = i;
		} else {
			tokens[i].family.siblings.prev = tokens[parent_i].family.children.last;
			tokens[tokens[parent_i].family.children.last].family.siblings.next = i;
			tokens[parent_i].family.children.last  = i;
		}
	}

	return i;
}
int jsmn_dom_delete(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i) {
	int parent_i;

	if (i >= (int) num_tokens) {
		return JSMN_ERROR_INVAL;
	}

	parent_i = tokens[i].family.parent;

	return 0;
}
int jsmn_dom_move(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int parent_i, int i) {
	int rc;

	rc = jsmn_dom_delete(parser, tokens, num_tokens, i);
	if (rc < 0) {
		return rc;
	}

	rc = jsmn_dom_add(parser, tokens, num_tokens, parent_i, i);
	if (rc < 0) {
		return rc;
	}

	return 0;
}
int jsmn_dom_set(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i, jsmntype_t type, int start, int end) {
	if (i >= (int) num_tokens) {
		return JSMN_ERROR_INVAL;
	}

	jsmn_fill_token(&tokens[i], type, start, end);

	return 0;
}
int jsmn_dom_close(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int i, int end) {
	if (i >= (int) num_tokens) {
		return JSMN_ERROR_INVAL;
	}

	if (jsmn_dom_is_open(parser, tokens, num_tokens, i)) {
		tokens[i].end = end;
	} else {
		return -1;
	}

	return 0;
}
int jsmn_dom_new(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens) {
	int i;
	jsmntok_t *tok;

	i = parser->toknext;

	tok = jsmn_alloc_token(parser, tokens, num_tokens);
	if (tok == NULL) {
		return JSMN_ERROR_NOMEM;
	}

	return i;	
}
int jsmn_dom_new_as(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, jsmntype_t type, int start, int end) {
	int rc;
	int i;

	i = jsmn_dom_new(parser, tokens, num_tokens);
	if (i < 0) {
		return i;
	}

	rc = jsmn_dom_set(parser, tokens, num_tokens, i, type, start, end);
	if (rc < 0) {
		return rc;
	}

	return i;
}
int jsmn_dom_new_object(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens) {
	return jsmn_dom_new_as(parser, tokens, num_tokens, JSMN_OBJECT, 0, 0);
}
int jsmn_dom_new_array(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens) {
	return jsmn_dom_new_as(parser, tokens, num_tokens, JSMN_ARRAY, 0, 0);
}
int jsmn_dom_new_primitive(jsmn_parser *parser, char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens, const char *value) {
	int i;
	int rc;
	size_t size;

	size = strlen(value);

	if (parser->pos + size + 1 >= len) {
		return JSMN_ERROR_NOMEM;
	}

	i = parser->toknext;

	memcpy(&js[parser->pos], value, size + 1);

	rc = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
	if (rc < 0) {
		return rc;
	}

	return i;
}
int jsmn_dom_new_string(jsmn_parser *parser, char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens, const char *value) {
	int i;
	int rc;
	size_t size;

	size = strlen(value);

	if (len <  parser->pos + 1 + size + 1 + 1) {
		return JSMN_ERROR_NOMEM;
	}

	i = parser->toknext;

	        js[parser->pos] = '"';
	memcpy(&js[parser->pos + 1], value, size);
	        js[parser->pos + 1 + size] = '"';
	        js[parser->pos + 1 + size + 1] = '\0';

	parser->pos++; /* skip past opening quote, where expected by `jsmn_parse_string()` */

	rc = jsmn_parse_string(parser, js, len, tokens, num_tokens);
	if (rc < 0) {
		return rc;
	}

	return i;
}
int jsmn_dom_eval(jsmn_parser *parser, char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens, const char *value) {
	int i;
	int rc;
	size_t size;

	size = strlen(value);

	if (parser->pos + size + 1 >= len) {
		return JSMN_ERROR_NOMEM;
	}

	i = parser->toknext;

	memcpy(&js[parser->pos], value, size + 1);

	rc = jsmn_parse(parser, js, len, tokens, num_tokens);
	if (rc < 0) {
		return rc;
	}

	return i;
}
int jsmn_dom_insert_name(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int object_i, int name_i, int value_i) {
	int rc;

	if (object_i >= (int) num_tokens || name_i >= (int) num_tokens || value_i >= (int) num_tokens) {
		return JSMN_ERROR_INVAL;
	}

	if (tokens[object_i].type != JSMN_OBJECT) {
		return JSMN_ERROR_INVAL;
	}

	rc = jsmn_dom_add(parser, tokens, num_tokens, object_i, name_i);
	if (rc < 0) {
		return rc;
	}

	rc = jsmn_dom_add(parser, tokens, num_tokens, name_i, value_i);
	if (rc < 0) {
		return rc;
	}

	return 0;
}
int jsmn_dom_insert_value(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int array_i, int value_i) {
	int rc;

	if (array_i >= (int) num_tokens || value_i >= (int) num_tokens) {
		return JSMN_ERROR_INVAL;
	}

	if (tokens[array_i].type != JSMN_ARRAY) {
		return JSMN_ERROR_INVAL;
	}

	rc = jsmn_dom_add(parser, tokens, num_tokens, array_i, value_i);
	if (rc < 0) {
		return rc;
	}

	return 0;
}
int jsmn_dom_delete_name(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int object_i, int name_i) {
	int rc;

	if (object_i >= (int) num_tokens || name_i >= (int) num_tokens) {
		return JSMN_ERROR_INVAL;
	}

	if (tokens[object_i].type != JSMN_OBJECT || jsmn_dom_get_parent(parser, tokens, num_tokens, name_i) != object_i) {
		return JSMN_ERROR_INVAL;
	}

	rc = jsmn_dom_delete(parser, tokens, num_tokens, name_i);
	if (rc < 0) {
		return rc;
	}

	return 0;
}
int jsmn_dom_delete_value(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens, int array_i, int value_i) {
	int rc;

	if (array_i >= (int) num_tokens || value_i >= (int) num_tokens) {
		return JSMN_ERROR_INVAL;
	}

	if (tokens[array_i].type != JSMN_ARRAY || jsmn_dom_get_parent(parser, tokens, num_tokens, value_i) != array_i) {
		return JSMN_ERROR_INVAL;
	}

	rc = jsmn_dom_delete(parser, tokens, num_tokens, value_i);
	if (rc < 0) {
		return rc;
	}

	return 0;
}
#endif

#ifdef JSMN_EMITTER

void jsmn_init_emitter(jsmn_emitter *emitter) {
	emitter->cursor_i = 0;
	emitter->cursor_phase = PHASE_UNOPENED;
}
int jsmn_emit(jsmn_parser *parser, char *js, size_t len,
		jsmntok_t *tokens, unsigned int num_tokens,
		jsmn_emitter *emitter, char *outjs, size_t outlen) {
	return 0;
}
#endif
