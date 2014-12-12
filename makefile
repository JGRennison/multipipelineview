prefix ?= /usr/local

NAME := multipipelineview

all: $(NAME)

VERSION_STRING := $(shell cat version 2>/dev/null || git describe --tags --always --dirty=-m 2>/dev/null || date "+%F %T %z" 2>/dev/null)
ifdef VERSION_STRING
CVFLAGS := -DVERSION_STRING='"${VERSION_STRING}"'
endif

CXXFLAGS ?= -Wall -Wextra -Wno-unused-parameter -O3 -g
LDFLAGS ?=
CPPFLAGS += -D_FILE_OFFSET_BITS=64
CXXFLAGS += -std=c++11

$(NAME): $(NAME).cpp
	g++ $(NAME).cpp $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(CVFLAGS) $(AFLAGS) -o $(NAME) $(LOADLIBES) $(LDLIBS)

.PHONY: all install uninstall clean dumpversion

dumpversion:
	@echo $(VERSION_STRING)

clean:
	rm -f $(NAME) $(NAME).1

install: $(NAME)
	install -D -m 755 $(NAME) $(DESTDIR)$(prefix)/bin/$(NAME)

uninstall:
	rm -f $(DESTDIR)$(prefix)/bin/$(NAME) $(DESTDIR)$(prefix)/share/man/man1/$(NAME).1

HELP2MANOK := $(shell help2man --version 2>/dev/null)
ifdef HELP2MANOK
all: $(NAME).1

$(NAME).1: $(NAME)
	help2man -s 1 -N ./$(NAME) -n "Multi Pipe Line View" -o $(NAME).1

install: install-man

.PHONY: install-man

install-man: $(NAME).1
	install -D -m 644 $(NAME).1 $(DESTDIR)$(prefix)/share/man/man1/$(NAME).1
	-mandb -pq

else
$(shell echo "Install help2man for man page generation" >&2)
endif
