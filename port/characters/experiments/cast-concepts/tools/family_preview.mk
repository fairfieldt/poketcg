$(BUILD_DIR)/model_options_preview.o: /Users/fairfieldt/dev/poketcg/port/characters/experiments/cast-concepts/tools/family_preview.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -I$(CURDIR) -MMD -c /Users/fairfieldt/dev/poketcg/port/characters/experiments/cast-concepts/tools/family_preview.c -o $@
