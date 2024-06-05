
.PHONY: format vm vm-noaccel

# Run this recipe to turn off KVM virtualization.
# Useful on (usually virtual) machines that do not support hardware acceleration.
vm-noaccel:
	./start-vm-no-accel.sh

vm:
	./start-vm.sh

# Run as "make format FMT_PATHS="../path/to/format/1 ../path/to/format/2"."
format:
ifeq ($(FMT_PATHS), )
	@echo No file to format, run \"make format FMT_PATHS=\"../path/to/format/1 ../path/to/format/2\".\"
else
	clang-format -i $(FMT_PATHS) --style=file:clang-format
endif
