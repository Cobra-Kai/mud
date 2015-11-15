CFLAGS = -Wall -W -O2 -g
all ::
.PHONY : all clean
well : CPPFLAGS += -D_GNU_SOURCE
well.OBJS = well.o grow.o object.o cencode.o cmd.o objdb.o
well : $(well.OBJS)
clean :: ; $(RM) well $(well.OBJS)
all :: well
test_object : test_object.c object.c cencode.c
all :: test_object
clean :: ; $(RM) test_object
