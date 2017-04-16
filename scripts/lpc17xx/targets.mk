# architecture-dependent additional targets and manual dependencies

# A little helper target for the maintainer =)
copy:
	cp $(TARGET).bin /mbed

bin:
	$(E) "  CHKSUM $(TARGET).bin"
	$(Q)scripts/lpc17xx/lpc_checksum.pl $(TARGET).bin

program: bin
	$(E) "  FLASH  $(TARGET).bin"
	$(Q)openocd -f .openocd-lpc.cfg -c init -c "flash_bin $(TARGET).bin" -c shutdown

# example cfg file for program target:
#    set CCLK 100000
#    source [find interface/openocd-usb.cfg]
#    source [find target/lpc1768.cfg]
#
#    jtag_khz 1000
#
#    proc wdtreset {} {
#        mww 0x40000004 0x100
#        mww 0x40000010 0x80000000
#        mww 0x40000000 3
#        mww 0x40000008 0xaa
#        mww 0x40000008 0x55
#    }
#
#    proc flash_bin { fname } {
#        halt
#        wait_halt
#        flash write_image erase unlock $fname
#        sleep 200
#        wdtreset
#    }

# Manual dependencies for the bootinfo include
$(OBJDIR)/src/lpc17xx/startup.o: src/lpc17xx/bootinfo.S

$(OBJDIR)/src/lpc17xx/pseudoboot.o: src/lpc17xx/bootinfo.S
