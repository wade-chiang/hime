.PHONY: all
all:

.PHONY: install
install:
	if [ $(prefix) = /usr/local ]; then \
	   install hime-setup.desktop hime-wayland.desktop /usr/share/applications; \
	   which update-menus > /dev/null 2>&1 && update-menus || exit 0; \
	else \
	   install -d "$(datadir)/applications"; \
	   install -m 644 hime-setup.desktop hime-wayland.desktop "$(datadir)/applications"; \
	fi

.PHONY: uninstall
uninstall:
	if [ $(prefix) = /usr/local ]; then \
	   cd /usr/share/applications && rm -f hime-setup.desktop hime-wayland.desktop; \
	else \
	   cd $(datadir)/applications && rm -f hime-setup.desktop hime-wayland.desktop && cd -; \
	   rmdir --ignore-fail-on-non-empty $(datadir)/applications; \
	fi

.PHONY: clean
clean:
	@echo "clean up"
	@rm -f $(MENUS) *~

