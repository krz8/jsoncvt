ME	= jsoncvt
SRCS	= main.c sanity.c twine.c ptrvec.c json.c xml.c ksh.c

OBJS	= $(SRCS:.c=.o)
DOCS	= jsoncvt.1 jsoncvt.html index.html jsonh.html

all:	$(ME)
$(ME):	$(OBJS)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(OBJS) $(LIBS)
docs:	$(DOCS)
clean:
	rm -f $(ME)
	rm -f $(OBJS)
	rm -f $(DOCS)
tags:
	etags $(SRCS)

json.o:		json.c sanity.h twine.h ptrvec.h json.h
ksh.o:		ksh.c sanity.h json.h ksh.h
main.o:		main.c sanity.h json.h xml.h ksh.h
ptrvec.o:	ptrvec.c sanity.h ptrvec.h
sanity.o:	sanity.c sanity.h
twine.o:	twine.c sanity.h twine.h
xml.o:		xml.c sanity.h json.h xml.h

.SUFFIXES:	.c .h .o .1 .adoc .html
.adoc.html:
	asciidoc $<
.adoc.1:
	a2x --format manpage $<
