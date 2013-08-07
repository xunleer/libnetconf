#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include "xmldiff.h"
#include "yinparser.h"

/**
 * @brief Allocate and initialize new xmldiff structure
 *
 * @return pointer to new xmldiff structure or NULL
 */
struct xmldiff * xmldiff_new ()
{
	struct xmldiff * new = calloc (1, sizeof (struct xmldiff));

	if (new == NULL) {
		return NULL;
	}
	
	new->diff_alloc = 10;
	new->diff_list = calloc (new->diff_alloc, sizeof (struct xmldiff_entry));
	
	if (new->diff_list == NULL) {
		free (new);
		return NULL;
	}

	return new;
}

/**
 * @breif Destroy and free whole xmldiff structure
 *
 * @param diff	pointer to xmldiff structure
 */
void xmldiff_free (struct xmldiff * diff)
{
	int i;
	for (i=0; i<diff->diff_count; i++) {
		free (diff->diff_list[i].path);
	}
	free (diff->diff_list);
	free (diff);
}	

const char * get_prefix (char * uri, const char * ns_mapping[])
{
	int i;

	for (i=0; ns_mapping[2*i] != NULL; i++) {
		if (strcmp(uri, ns_mapping[2*i+1]) == 0) {
			return(ns_mapping[2*i]);
		}
	}
	return(NULL);
}

/**
 * @brief Add single diff record
 *
 * @param diff	pointer to xmldiff structure
 * @param[in] path	path in XML document where the change occurs
 * @param[in]	op	change type
 *
 * return EXIT_SUCCESS or EXIT_FAILURE
 */
int xmldiff_add_diff (struct xmldiff * diff, const char * ns_mapping[], const char * path, xmlNodePtr node, XMLDIFF_OP op)
{
	struct xmldiff_entry * internal;
	xmlNodePtr child;
	char * new_path;
	int i;

	/* if added or removed mark all children the same */
	if (op == XMLDIFF_ADD || op == XMLDIFF_REM) {
		for (child = node->children; child != NULL; child = child->next) {
			if (child->type != XML_ELEMENT_NODE) {
				continue;
			}
			asprintf (&new_path, "%s/%s:%s", path, get_prefix((char*)child->ns->href, ns_mapping), child->name);
			xmldiff_add_diff (diff, ns_mapping, new_path, child, op);
			free(new_path);
		}
	}

	/* check for duplicit diffs */
	for (i = 0; i < diff->diff_count; ++i) {
		/* should be enough to check the node pointer only */
		if (diff->diff_list[i].node == node) {
			MSG(0, "Duplicit found.");
			return EXIT_SUCCESS;
		}
	}

	if (diff->diff_count == diff->diff_alloc) {
		diff->diff_alloc *= 2;
		internal = realloc (diff->diff_list, sizeof (struct xmldiff_entry) * diff->diff_alloc);
		if (internal == NULL) {
			return EXIT_FAILURE;
		}
		diff->diff_list = internal;
	}

	diff->diff_list[diff->diff_count].path = strdup (path);
	diff->diff_list[diff->diff_count].node = node;
	diff->diff_list[diff->diff_count].op = op;

	diff->diff_count++;

	return EXIT_SUCCESS;
}

#if 0
XMLDIFF_OP xmldiff_list
{
	/* TODO: Move code for list processing from xmldiff recursive here*/
}

XMLDIFF_OP xmldiff_leaflist ()
{
	/* TODO: Move code for leaf-list processing from xmldiff recursive here*/
}
#endif

/**
 * @brief Recursively go through documents and search for differences
 *
 * @param
 */
XMLDIFF_OP xmldiff_recursive (struct xmldiff *diff, const char *ns_mapping[], char * path, xmlDocPtr old_doc, xmlNodePtr old_node, xmlDocPtr new_doc, xmlNodePtr new_node, struct model_tree * model)
{
	char * next_path;
	int i;
	xmlNodePtr old_tmp, new_tmp;
	xmlNodePtr list_old_tmp, list_new_tmp, list_old_inter, list_new_inter;
	XMLDIFF_OP tmp_op, ret_op = XMLDIFF_NONE, item_ret_op;
	xmlChar * old_content, * new_content;
	xmlChar * old_str, *new_str;
	xmlChar * old_keys, *new_keys;
	xmlBufferPtr buf;

	/* Some of the required documents are missing */
	if (old_doc == NULL || new_doc == NULL || model == NULL) {
		return XMLDIFF_ERR;
	}

	/* On YIN_TYPE_CHOICE the choice element itself is found in neither old config nor new config, but should be further parsed */
	if (model->type == YIN_TYPE_CHOICE) {
		goto model_type;
	}

	/* Find the node from the model in the old configuration */
	old_tmp = old_node;
	while (old_tmp) {
		if (xmlStrEqual (old_tmp->name, BAD_CAST model->name)) {
			break;
		}
		old_tmp = old_tmp->next;
	}

	/* Find the node from the model in the new configuration */
	new_tmp = new_node;
	while (new_tmp) {
		if (xmlStrEqual (new_tmp->name, BAD_CAST model->name)) {
			break;
		}
		new_tmp = new_tmp->next;
	}

	if (old_tmp == NULL && new_tmp == NULL) { /* Node is not in the new config nor the old config */
		return XMLDIFF_NONE;

	} else if (old_tmp == NULL) { /* Node was added */
		xmldiff_add_diff(diff, ns_mapping, path, new_tmp, XMLDIFF_ADD);
		return XMLDIFF_ADD;

	} else if (new_tmp == NULL) { /* Node was removed */
		xmldiff_add_diff(diff, ns_mapping, path, old_tmp, XMLDIFF_REM);
		return XMLDIFF_REM;

	} else { /* Node is in both configurations, check for internal changes */
model_type:
		switch (model->type) {
		case YIN_TYPE_CONTAINER:
		/* Container */	
		ret_op = XMLDIFF_NONE;
			for (i=0; i<model->children_count; i++) {
				asprintf (&next_path, "%s/%s:%s", path, model->children->ns_prefix, model->children[i].name);
				tmp_op = xmldiff_recursive (diff, ns_mapping, next_path, old_doc, old_tmp->children, new_doc, new_tmp->children, &model->children[i]);
				free (next_path);
	
				if (tmp_op == XMLDIFF_ERR) {
					return XMLDIFF_ERR;
				} else if (tmp_op != XMLDIFF_NONE) {
					ret_op = XMLDIFF_CHAIN;
				}
			}
			if (ret_op == XMLDIFF_CHAIN) {
				xmldiff_add_diff (diff, ns_mapping, path, new_tmp, XMLDIFF_CHAIN);
			}
			break;
		case YIN_TYPE_CHOICE: 
			/* Choice */ 
			ret_op = XMLDIFF_NONE;
			/* Trim the choice path, replace it with the children directly */
			*strrchr(path, '/') = '\0';

			for (i=0; i<model->children_count; i++) {
				asprintf (&next_path, "%s/%s:%s", path, model->children->ns_prefix, model->children[i].name);
				/* We are moving down the model only (not in the configuration) */
				tmp_op = xmldiff_recursive (diff, ns_mapping, next_path, old_doc, old_node, new_doc, new_node, &model->children[i]);
				free (next_path);

				/* Assuming there is only one child of this choice (as it should be), we return this child's operation, the choice itself is de-facto skipped */
				if (tmp_op != XMLDIFF_NONE) {
					ret_op = tmp_op;
					break;
				}
			}

			break;
		case YIN_TYPE_LEAF: 
		 	/* Leaf */
			old_content = xmlNodeGetContent (old_tmp);
			new_content = xmlNodeGetContent (new_tmp);
			if (xmlStrEqual (old_content, new_content)) {
				ret_op = XMLDIFF_NONE;
			} else {
				ret_op = XMLDIFF_MOD;
				xmldiff_add_diff (diff, ns_mapping, path, new_tmp, XMLDIFF_MOD);
			}
			xmlFree (old_content);
			xmlFree (new_content);
			break;
		case YIN_TYPE_LIST:
			/* List */
			ret_op = XMLDIFF_NONE;

			/* Find matches according to the key elements, process all the elements inside recursively */
			/* Not matching are _ADD or _REM */
			/* Maching are _NONE or _CHAIN, according to the return values of the recursive calls */

			/* Go through the old nodes and search for matching nodes in the new document*/
			list_old_tmp = old_tmp;
			while (list_old_tmp) {
				item_ret_op = XMLDIFF_NONE;
				/* For every old node create string holding the concatenated key values */
				old_keys = BAD_CAST strdup ("");
				for (i=0; i<model->keys_count; i++) { /* For every specified key */
					list_old_inter = list_old_tmp->children;
					while (list_old_inter) {
						if (xmlStrEqual(list_old_inter->name, BAD_CAST model->keys[i])) { /* Find matching leaf in old document */
							old_str = xmlNodeGetContent (list_old_inter);
							old_keys  = realloc (old_keys, sizeof(char) * (strlen((const char*)old_keys)+strlen((const char*)old_str)+1));
							strcat ((char*)old_keys, (char*)old_str); /* Concatenate key value */
							xmlFree (old_str);
							break;
						}
						list_old_inter = list_old_inter->next;
					}
				}
				old_keys  = realloc (old_keys, sizeof(char) * (strlen((const char*)old_keys)+strlen((const char*)list_old_tmp->name)+1));
				strcat ((char*)old_keys, (char*)list_old_tmp->name); /* !! Concatenate the node's name, the only positively unique key is the tuple keys + name */
				/* Go through the new list */
				list_new_tmp = new_tmp;
				while (list_new_tmp) {
					new_keys = BAD_CAST strdup ("");
					for (i=0; i<model->keys_count; i++) {
						list_new_inter = list_new_tmp->children;
						while (list_new_inter) {
							if (xmlStrEqual(list_new_inter->name, BAD_CAST model->keys[i])) {
								new_str = xmlNodeGetContent (list_new_inter);
								new_keys  = realloc (new_keys, sizeof(char) * (strlen((const char*)new_keys)+strlen((const char*)new_str)+1));
								strcat ((char*)new_keys, (char*)new_str);
								xmlFree (new_str);
								break;
							}
							list_new_inter = list_new_inter->next;
						}
					}
					new_keys  = realloc (new_keys, sizeof(char) * (strlen((const char*)new_keys)+strlen((const char*)list_new_tmp->name)+1));
					strcat ((char*)new_keys, (char*)list_new_tmp->name);
					if (strcmp ((const char*)old_keys, (const char*)new_keys) == 0) { /* Matching item found */
						free (new_keys);
						break;
					}
					free (new_keys);
					list_new_tmp = list_new_tmp->next;
				}
				free (old_keys);

				if (list_new_tmp == NULL) { /* Item NOT found in the new document -> removed */
					xmldiff_add_diff (diff, ns_mapping, path, list_old_tmp, XMLDIFF_REM);
					ret_op = XMLDIFF_CHAIN;
				} else { /* Item found -> check for changes recursively*/
					for (i=0; i<model->children_count; i++) {
						asprintf (&next_path, "%s/%s:%s", path, model->children->ns_prefix, model->children[i].name);
						tmp_op = xmldiff_recursive (diff, ns_mapping, next_path, old_doc, list_old_tmp->children, new_doc, list_new_tmp->children, &model->children[i]);
						free (next_path);

						if (tmp_op == XMLDIFF_ERR) {
							return XMLDIFF_ERR;
						} else {
							item_ret_op |= tmp_op;
						}
					}

					if (item_ret_op != XMLDIFF_NONE) {
						xmldiff_add_diff (diff, ns_mapping, path, list_new_tmp, XMLDIFF_CHAIN);
						ret_op = XMLDIFF_CHAIN;
					}
				}
				list_old_tmp = list_old_tmp->next;
			}

			/* Go through the new nodes and search for matching nodes in the old document*/
			list_new_tmp = new_tmp;
			while (list_new_tmp) {
				item_ret_op = XMLDIFF_NONE;
				/* For every new node create string holding the concatenated key values */
				new_keys = BAD_CAST strdup ("");
				for (i=0; i<model->keys_count; i++) { /* For every specified key */
					list_new_inter = list_new_tmp->children;
					while (list_new_inter) {
						if (xmlStrEqual(list_new_inter->name, BAD_CAST model->keys[i])) { /* Find matching leaf in the old document */
							new_str = xmlNodeGetContent (list_new_inter);
							new_keys  = realloc (new_keys, sizeof(char) * (strlen((const char*)new_keys)+strlen((const char*)new_str)+1));
							strcat ((char*)new_keys, (char*)new_str); /* Concatenate key value */
							xmlFree (new_str);
							break;
						}
						list_new_inter = list_new_inter->next;
					}
				}
				new_keys  = realloc (new_keys, sizeof(char) * (strlen((const char*)new_keys)+strlen((const char*)list_new_tmp->name)+1));
				strcat ((char*)new_keys, (char*)list_new_tmp->name);
				/* Go through the new list */
				list_old_tmp = old_tmp;
				while (list_old_tmp) {
					old_keys = BAD_CAST strdup ("");
					for (i=0; i<model->keys_count; i++) {
						list_old_inter = list_old_tmp->children;
						while (list_old_inter) {
							if (xmlStrEqual(list_old_inter->name, BAD_CAST model->keys[i])) {
								old_str = xmlNodeGetContent (list_old_inter);
								old_keys  = realloc (old_keys, sizeof(char) * (strlen((const char*)old_keys)+strlen((const char*)old_str)+1));
								strcat ((char*)old_keys, (char*)old_str);
								xmlFree (old_str);
								break;
							}
							list_old_inter = list_old_inter->next;
						}
					}
					old_keys  = realloc (old_keys, sizeof(char) * (strlen((const char*)old_keys)+strlen((const char*)list_old_tmp->name)+1));
					strcat ((char*)old_keys, (char*)list_old_tmp->name);
					if (strcmp ((const char*)old_keys, (const char*)new_keys) == 0) { /* Matching item found */
						free (old_keys);
						break;
					}
					free (old_keys);
					list_old_tmp = list_old_tmp->next;
				}
				free (new_keys);
				
				if (list_old_tmp == NULL) { /* Item NOT found in the old document -> added */
					xmldiff_add_diff (diff, ns_mapping, path, list_new_tmp, XMLDIFF_ADD);
					ret_op = XMLDIFF_CHAIN;
				} else {
					/* We already checked for changes in these nodes */
				}
				list_new_tmp = list_new_tmp->next;
			}
			break;
		case YIN_TYPE_LEAFLIST:
			/* Leaf-list */
			ret_op = XMLDIFF_NONE;

			/* Search for matches, only _ADD and _REM will be here */
			/* For each in the old node find one from the new nodes or log as _REM */
			list_old_tmp = old_tmp;
			while (list_old_tmp) {
				old_str = xmlNodeGetContent(list_old_tmp);
				list_new_tmp = new_tmp;
				while (list_new_tmp) {
					new_str = xmlNodeGetContent (list_new_tmp);
					if (xmlStrEqual (old_str, new_str)) {
						xmlFree (new_str);
						/* Equivalent found */
						break;
					}
					xmlFree (new_str);
					list_new_tmp = list_new_tmp->next;
				}
				xmlFree (old_str);
				if (list_new_tmp == NULL) {
					xmldiff_add_diff (diff, ns_mapping, path, list_old_tmp, XMLDIFF_REM);
					ret_op = XMLDIFF_CHAIN;
				}
				list_old_tmp = list_old_tmp->next;
			}
			/* For each in the new node find one from the old nodes or log as _ADD */
			list_new_tmp = new_tmp;
			while (list_new_tmp) {
				new_str = xmlNodeGetContent(list_new_tmp);
				list_old_tmp = old_tmp;
				while (list_old_tmp) {
					old_str = xmlNodeGetContent (list_old_tmp);
					if (xmlStrEqual (old_str, new_str)) {
						xmlFree (old_str);
						/* Equivalent found */
						break;
					}
					xmlFree (old_str);
					list_old_tmp = list_old_tmp->next;
				}
				xmlFree (new_str);
				if (list_old_tmp == NULL) {
					xmldiff_add_diff (diff, ns_mapping, path, list_new_tmp, XMLDIFF_ADD);
					ret_op = XMLDIFF_CHAIN;
				}
				list_new_tmp = list_new_tmp->next;
			}
			break;
		case YIN_TYPE_ANYXML:
			/* Anyxml */
			/* Serialize and compare strings */
			/* TODO: find better solution in future */
			buf = xmlBufferCreate ();
			xmlNodeDump (buf, old_doc, old_tmp, 0, 0);
			old_str = xmlStrdup(xmlBufferContent(buf));
			xmlBufferEmpty (buf);
			xmlNodeDump (buf, new_doc, new_tmp, 0, 0);
			new_str = xmlStrdup(xmlBufferContent(buf));
			xmlBufferFree (buf);

			if (xmlStrEqual(old_str, new_str)) {
				ret_op = XMLDIFF_NONE;
			} else {
				xmldiff_add_diff (diff, ns_mapping, path, new_tmp, XMLDIFF_MOD);
				ret_op = XMLDIFF_CHAIN;
			}
			xmlFree(old_str);
			xmlFree(new_str);
			break;
		default:
			/* No other type is supported now */
			break;
		}
	}

	return ret_op;
}

/**
 * @brief Module top level function
 *
 * @param old		old version of XML document
 * @param new		new version of XML document
 * @param model	data model in YANG format
 *
 * @return xmldiff structure holding all differences between XML documents or NULL
 */
struct xmldiff * xmldiff_diff (xmlDocPtr old, xmlDocPtr new, struct model_tree * model, const char * ns_mapping[])
{
	struct xmldiff * diff;
	char * path;

	if (old == NULL || new == NULL) {
		return NULL;
	}

	if ((diff = xmldiff_new ()) == NULL) {
		return NULL;
	}
	
	asprintf (&path, "/%s:%s", model->children->ns_prefix, model->children->name);
	diff->all_stat = xmldiff_recursive (diff, ns_mapping, path, old, old->children, new, new->children, &model->children[0]);
	free (path);

	return diff;
}
