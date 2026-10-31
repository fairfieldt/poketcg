.DEFAULT_GOAL := all
BLENDER ?= /Applications/Blender.app/Contents/MacOS/Blender
PYTHON ?= python3
.PHONY: all catalog parts models model test sheet cast gallery capture index test-blender
all: models test sheet cast index
catalog:
	$(PYTHON) tools/catalog.py
parts:
	"$(BLENDER)" --background --python-exit-code 1 --python tools/create_parts.py
models:
	$(PYTHON) tools/build_all.py --blender "$(BLENDER)"
model:
	$(PYTHON) tools/build_all.py --blender "$(BLENDER)" --only "$(NAME)" --force
test:
	$(PYTHON) tools/validate.py
	$(PYTHON) tools/check_skin.py
sheet:
	$(PYTHON) tools/review_sheet.py
cast:
	"$(BLENDER)" --background --python-exit-code 1 --python tools/create_cast.py
gallery:
	$(PYTHON) tools/pack_gallery.py
	$(MAKE) -C gallery -j4
capture: gallery
	$(MAKE) -C gallery capture
index:
	$(PYTHON) tools/bindings.py
	$(PYTHON) tools/model_index.py
test-blender:
	"$(BLENDER)" assets/cast.blend --background --python-exit-code 1 --python tools/check_blender.py
