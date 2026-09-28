# hime-ibus: HIME as an IBus engine (GNOME), a HIME client like the IM
# modules.  IBus starts it as the component hime.xml describes.
IBUSINC=$(shell pkg-config --cflags ibus-1.0)
IBUSLIBS=$(shell pkg-config --libs ibus-1.0)
COMPONENTDIR=$(datadir)/ibus/component

CFLAGS += $(OPTFLAGS) $(IBUSINC) \
		-DCLIENT_LIB=1 \
		-DFREEBSD=$(FREEBSD) \
		-DHIME_VERSION=\"$(HIME_VERSION)\" \
		-I../im-client -I.. \
		-I../IMdkit/include

%.o: %.c
	@echo "  $< -> $@"
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

.PHONY: all
all: hime-ibus hime.xml

OBJS = hime-ibus.o gnome-app-monitor.o

hime-ibus: $(OBJS) ../im-client/libhime-im-client.so
	@echo "linking $@ ..."
	$(CC) $(gcc_ld_run_path) $(OBJS) -L../im-client -lhime-im-client -o $@ $(IBUSLIBS)

# the paths are those of the installed files
hime.xml: hime.xml.in
	sed -e 's|%HIME_IBUS%|$(himelibdir)/hime-ibus|' \
		-e 's|%HIME_VERSION%|$(HIME_VERSION)|' \
		-e 's|%HIME_ICON%|$(SYS_ICON_DIR)/hime.png|' $< >$@

.PHONY: clean
clean:
	@echo "clean up"
	@rm -f *.o *.d hime-ibus hime.xml *~ core.*

.PHONY: install
install:
	install -d "$(himelibdir)" "$(COMPONENTDIR)"
	install -m 755 hime-ibus "$(himelibdir)"
	install -m 644 hime.xml "$(COMPONENTDIR)"

.PHONY: uninstall
uninstall:
	rm -f "$(himelibdir)/hime-ibus" "$(COMPONENTDIR)/hime.xml"

CFLAGS += -MMD -MP
-include $(wildcard *.d)
