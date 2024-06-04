
.PHONY: format vm

# Run as "make vm accel=no" to turn off KVM on (usually virtual) machines
# that do not support hardware acceleration.
vm:
ifeq ($(accel),no)
	./start-vm-no-accel.sh
else
	./start-vm.sh
endif

# Run as "make format FMT_PATHS="../path/to/format/1 ../path/to/format/2"."
format:
	clang-format -i $(FMT_PATHS) --style=file:clang-format
