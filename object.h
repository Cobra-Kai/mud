#ifndef OBJECT_H
#define OBJECT_H
struct object;
struct object_iter {
	struct object *o;
	unsigned i;
};
struct object *obj_new(void);
void obj_retain(struct object *o);
void obj_release(struct object *o);
void obj_free(struct object *o);
const char *obj_get(struct object *o, const char *name);
int obj_set(struct object *o, const char *name, const char *value);
struct object_iter obj_iter_new(struct object *o);
int obj_iter_next(struct object_iter *it, const char **name, const char **value);
int obj_save(struct object *o, FILE *f);
struct object *obj_load(FILE *f, const char *tag);
#endif
