#ifndef OBJDB_H
#define OBJDB_H
#include <stdio.h>

struct objdb_txn;

struct objdb_txn *objdb_start(const char *path);
FILE *objdb_f(struct objdb_txn *txn);
struct object *objdb_load(const char *path);
int objdb_commit(struct objdb_txn *txn);
int objdb_rollback(struct objdb_txn *txn);
int objdb_setroot(const char *path);
#endif
