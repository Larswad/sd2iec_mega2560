# architecture-dependent additional targets and manual dependencies

# Program the device.
program: bin hex eep
	$(AVRDUDE) $(AVRDUDE_FLAGS) $(AVRDUDE_WRITE_FLASH)  $(AVRDUDE_WRITE_EEPROM)

# Set fuses of the device
fuses: $(CONFIG)
	$(AVRDUDE) $(AVRDUDE_FLAGS) $(AVRDUDE_WRITE_FUSES)


# Manual dependency for the assembler module
$(OBJDIR)/fastloader-ll.o: include/config.h include/fastloader.h $(OBJDIR)/autoconf.h

