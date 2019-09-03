SSC = ./build/compiler
SSA = ./build/assembler

STD_SRC_DIR   = lib/std
STD_DIR       = $(OUTPUT)/std
STD_START_SSO = $(STD_DIR)/_start.sso

ssc = $(SSC) $< $@
ssa = $(SSA) $< $@


$(STD_DIR):
	@[ -e $@ ] || mkdir -p $@

$(STD_DIR)/core: $(STD_DIR)
	@[ -e $@ ] || mkdir $@

$(STD_DIR)/_start.sso: $(STD_SRC_DIR)/_start.ssa | assembler
	$(ssa)

stdlib: $(STD_DIR)/_start.sso
