EXECUTABLE :=  redis-server 							        # 可执行文件名
LIBDIR := #src/lib/libst.a           							# 静态库目录
LIBS :=  pthread crypt z    					    # 静态库文件名
INCLUDES:= . src 		# 头文件目录
SRCDIR:= src   			    # 除了当前目录外，其他的源代码文件目录
TEST := redis-server

RM-F := rm -f

CC:=gcc
CFLAGS := -g -Wall -O3
CPPFLAGS := $(CFLAGS)
CPPFLAGS += $(addprefix -I,$(INCLUDES))
CPPFLAGS += -MMD

# # You shouldn't need to change anything below this point.
#
SRCS := $(wildcard *.c) $(wildcard $(addsuffix /*.c, $(SRCDIR))) #list all *.cc in the current dir and SRCDIR
OBJS := $(patsubst %.c, %.o, $(SRCS)) # alt cc to o in SRCS, patsubst is a function
DEPS := $(patsubst %.o, %.d, $(OBJS)) #alt o to d in OBJS
MISSING_DEPS := $(filter-out $(wildcard $(DEPS)),$(DEPS)) #remove $(wildcard $(DEPS)) from $(DEPS) then return
MISSING_DEPS_SOURCES := $(wildcard $(patsubst %.d,%.c,$(MISSING_DEPS))) #alt missing *.d to *.cc


.PHONY : all deps objs clean veryclean rebuild info

all: $(EXECUTABLE)

deps: $(DEPS)

objs: $(OBJS)

clean:
	@$(RM-F) $(OBJS)
	@$(RM-F) $(DEPS)

veryclean: clean
	@$(RM-F) $(EXECUTABLE)

rebuild: veryclean all
ifneq ($(MISSING_DEPS),)
$(MISSING_DEPS) :
	@$(RM-F) $(patsubst %.d,%.o,$@)
endif
-include $(DEPS)
$(EXECUTABLE): $(OBJS)
	$(CC) -o $(EXECUTABLE) $(OBJS)  $(LIBDIR) $(addprefix -l,$(LIBS)) 

$(OBJS):%.o:%.c
	$(CC) $(CPPFLAGS) -o $@ -c $< 
 
info:
	@echo $(SRCS)
	@echo $(OBJS)
	@echo $(DEPS)
	@echo $(MISSING_DEPS)
	@echo $(MISSING_DEPS_SOURCES)