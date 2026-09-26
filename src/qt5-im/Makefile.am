if FREEBSD
  INCS=-I../im-client -I/usr/local/include
else
  INCS=-I../im-client -I/usr/include/X11
endif

QTINC=$(shell pkg-config --variable=includedir Qt5Core)
QTVER=$(shell pkg-config --modversion Qt5Core)

INCS+=$(shell pkg-config Qt5Core Qt5Gui Qt5DBus --cflags)
INCS+=-I$(QTINC)/QtGui/$(QTVER)
INCS+=-I$(QTINC)/QtCore/$(QTVER)

CXXFLAGS+=$(OPTFLAGS) $(INCS) \
		-D_REENTRANT \
		-DQT5 \
		-DQT_SHARED \
		-DQT_IMMODULE \
		-DPIC -fPIC

OBJS= moc_hime-qt.o hime-qt.o hime-imcontext-qt.o moc_hime-imcontext-qt.o

QTLIBS=$(shell pkg-config Qt5Core Qt5Gui Qt5DBus --libs)
LDFLAGS+=$(LDFLAGS_ENV) -L../im-client -lhime-im-client $(QTLIBS)

# Drives the input context as the platform plugin would (tests/session/).
QT_IM_TEST=../../tests/session/qt5-im-test
# Qt 5 private headers need qplatformdefs.h from the mkspec
QT_MKSPEC=$(shell $(QT5_QMAKE_PATH) -query QT_HOST_DATA)/mkspecs/$(shell $(QT5_QMAKE_PATH) -query QMAKE_XSPEC)

$(QT_IM_TEST): ../../tests/session/qt-im-test.cpp
	$(CXX) -std=c++17 $(CXXFLAGS) -I$(QTINC)/QtGui/$(QTVER)/QtGui -I$(QTINC)/QtCore/$(QTVER)/QtCore \
		-I$(QT_MKSPEC) -o $@ $< $(QTLIBS)

.PHONY: qt-im-test
qt-im-test: $(QT_IM_TEST)

.PHONY: all
all:    qt_info im-hime.so

%.E: %.cpp
	@echo "  $< -> $@"
	@$(CXX) -E $(CFLAGS) $(INCS) $< > $@

%.o: %.cpp
	@echo "  $< -> $@"
	@$(CXX) -c -o $@ $(CXXFLAGS) $<

moc_hime-qt.cpp:	hime-qt.h
	@echo "  $< -> $@"
	$(QT5_MOC_PATH) $(INCS) $< -o moc_hime-qt.cpp

moc_hime-imcontext-qt.cpp:	hime-imcontext-qt.h
	@echo "  $< -> $@"
	$(QT5_MOC_PATH) $(INCS) $< -o moc_hime-imcontext-qt.cpp

im-hime.so: $(OBJS)
	@echo "linking $@ ..."
	@export LD_RUN_PATH=$(himelibdir) ;\
		$(CXX) $(gcc_ld_run_path) -fno-exceptions -shared -o $@ $(OBJS) $(LDFLAGS)
	@rm -f core.*

.PHONY: qt_info
qt_info:
	@echo "Qt toolchain info:"
	@echo ""
	@echo "Qt version: $(QTVER)"
	@echo "include path: $(QTINC)"
	@echo "full include path: $(INCS)"
	@echo ""

.PHONY: install
install:
	install -d "$(QT5_IM_DIR)"; \
	install -m 755 im-hime.so "$(QT5_IM_DIR)"

.PHONY: uninstall
uninstall:
	rm -f $(QT5_IM_DIR)/im-hime.so
	rmdir --ignore-fail-on-non-empty $(QT5_IM_DIR)

.PHONY: clean
clean:
	@echo "clean up"
	@rm -f $(OBJS) *.d im-hime.so *~ core.* *moc*.cpp *.E .depend $(QT_IM_TEST) $(QT_IM_TEST).d
	@rm -f moc_hime-qt.cpp moc_hime-imcontext-qt.cpp

.depend:
	@echo "building $@ ..."
	@$(CXX) $(CXXFLAGS) -MM *.cpp > $@

# Rebuild objects when a header they include changes: the compiler writes
# the dependencies of each object into a .d file next to it.
CXXFLAGS += -MMD -MP
-include $(wildcard *.d)
