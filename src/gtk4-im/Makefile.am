OBJS = imhime.o gtkimcontexthime.o
GTKINC=$(shell pkg-config --cflags gtk4)
LDFLAGS=$(shell pkg-config --libs gtk4)
GTK4_LIBDIR=$(shell pkg-config --variable=libdir gtk4)
GTK4_BINARY_VERSION=$(shell pkg-config --variable=gtk_binary_version gtk4)
MODULEDIR=$(DESTDIR)/$(GTK4_LIBDIR)/gtk-4.0/$(GTK4_BINARY_VERSION)/immodules
# GTK 4 loads IM modules through GIO, which requires this file name.
MODULE=libim-hime.so


CFLAGS += $(OPTFLAGS) $(GTKINC) \
		-DG_DISABLE_SINGLE_INCLUDES \
		-DCLIENT_LIB=1 \
		-DFREEBSD=$(FREEBSD) \
		-I../im-client -I.. \
		-I../IMdkit/include

%.o: %.c
	@echo "  $< -> $@"
	$(CC) -c -fPIC $(CPPFLAGS) $(CFLAGS) -o $@ $<

%.E: %.c
	@echo "  $< -> $@"
	$(CC) -E $(CPPFLAGS) $(CFLAGS) -o $@ $<

.PHONY: all
all:	$(MODULE)

$(MODULE):     $(OBJS) ../im-client/libhime-im-client.so
	@echo "linking $@ ..."
	$(CC) $(gcc_ld_run_path) $(SO_FLAGS) $(OBJS) -L../im-client -lhime-im-client -o $@ $(LDFLAGS)
	@rm -f core.*

.PHONY: clean
clean:
	@echo "clean up"
	@rm -f *.o *.so *~ core.* .depend *.E

.PHONY: install
install:
	install -d "$(MODULEDIR)"
	install -m 755 $(MODULE) "$(MODULEDIR)"

.PHONY: uninstall
uninstall:
	rm -f $(MODULEDIR)/$(MODULE)
	rmdir --ignore-fail-on-non-empty $(MODULEDIR)

.depend:
	@echo "building $@ ..."
	$(CC) $(CPPFLAGS) $(CFLAGS) -MM *.c > $@
