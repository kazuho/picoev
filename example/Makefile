all : picoev_echo
	@echo done

picoev_echo: picoev_echo.c
	@echo $@
	@gcc -o picoev_echo -I.. $^ -static -L.. -lpicoev

.PHONY: clean

clean:
	@rm -rf picoev_echo
