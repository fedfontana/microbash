EXEC = microbash

CCOMP = gcc
DEFAULTFLAGS = -lreadline -ansi -std=gnu11
DEBUGFLAGS = -Wall -Wextra -Werror -pedantic

debug: $(EXEC).c
	@$(CCOMP) $^ $(DEFAULTFLAGS) $(DEBUGFLAGS) -g -o $(EXEC).out

run: build
	@./$(EXEC).out

build: $(EXEC).c
	@$(CCOMP) $^ $(DEFAULTFLAGS) -O4 -o $(EXEC).out

asan: $(EXEC).c
	@$(CCOMP) $^ $(DEFAULTFLAGS) $(DEBUGFLAGS) -g -fsanitize=address -o $(EXEC).out

clean:
	rm *.out
	rm *.txt

.PHONY = debug asan build run clean
