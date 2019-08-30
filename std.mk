SSC = ./build/compiler
SSA = ./build/assembler

STD_SRC_DIR   = lib/std
STD_DIR       = $(OUTPUT)/std
STD_START_SSO = $(STD_DIR)/_start.sso

ssc = $(SSC) $< $@
ssa = $(SSA) $< $@


$(STD_DIR):
	[ -e $@ ] || mkdir -p $@

$(STD_DIR)/core:					| $(STD_DIR)
	[ -e $@ ] || mkdir $@

$(STD_DIR)/core/io.sso:	$(STD_SRC_DIR)/core/io.ssa	| $(STD_DIR)/core
	$(ssa)

$(STD_DIR)/io.sso:	$(STD_SRC_DIR)/io.sst	$(STD_DIR)/core/io.sso
	$(SSC)	$<			/tmp/std/io.ssa
	$(SSA)	/tmp/io.ssa		$@
	rm	/tmp/io.ssa

$(STD_DIR)/_start.sso: $(STD_SRC_DIR)/_start.ssa | assembler
	$(ssa)

stdlib: $(STD_DIR)/_start.sso $(STD_DIR)/core/io.sso
