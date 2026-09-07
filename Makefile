#---------------------------------------------------------------------------------
# ChargeCap
#
#   make            build sysmodule + overlay
#   make dist       build and lay out an SD-card-ready tree in dist/
#   make zip        build and produce ChargeCap.zip
#   make clean      clean both builds
#---------------------------------------------------------------------------------

TITLE_ID   := 42000000000000C0
DIST       := dist
ZIP        := ChargeCap.zip
CONTENTS   := $(DIST)/atmosphere/contents/$(TITLE_ID)

.PHONY: all sysmodule overlay libultrahand dist zip clean

all: sysmodule overlay

libultrahand:
	@./scripts/vendor-libultrahand.sh

sysmodule:
	@$(MAKE) --no-print-directory -C sysmodule

overlay: libultrahand
	@$(MAKE) --no-print-directory -C overlay

dist: all
	@[ -f sysmodule/out/chargecap.nsp ] || (echo "ERROR: sysmodule did not build"; exit 1)
	@[ -f overlay/out/ChargeCap.ovl ] || (echo "ERROR: overlay did not build"; exit 1)
	@rm -rf $(DIST)
	@mkdir -p $(CONTENTS)/flags
	@mkdir -p $(DIST)/switch/.overlays
	@cp sysmodule/out/chargecap.nsp $(CONTENTS)/exefs.nsp
	@cp sysmodule/toolbox.json $(CONTENTS)/toolbox.json
	@touch $(CONTENTS)/flags/boot2.flag
	@cp overlay/out/ChargeCap.ovl $(DIST)/switch/.overlays/ChargeCap.ovl
	@echo "dist tree ready in $(DIST)/ - copy its contents to the root of your SD card"

zip: dist
	@rm -f $(ZIP)
	@cd $(DIST) && zip -qr ../$(ZIP) .
	@echo "built ... $(ZIP)"

clean:
	@$(MAKE) --no-print-directory -C sysmodule clean
	@$(MAKE) --no-print-directory -C overlay clean || true
	@rm -rf $(DIST) $(ZIP)
