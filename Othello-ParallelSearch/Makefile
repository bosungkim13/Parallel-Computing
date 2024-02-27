NOWARN=-wd3946 -wd3947 -wd10010

EXEC=othello
OBJ =  $(EXEC) $(EXEC)-debug $(EXEC)-serial

# flags
OPT=-O2 -g $(NOWARN)
DEBUG=-O0 -g $(NOWARN)

# --- set number of workers to non-default value
ifneq ($(W),)
XX=CILK_NWORKERS=$(W)
endif

I=default_input

all: $(OBJ)

# build the debug parallel version of the program
$(EXEC)-debug: $(EXEC).cpp
	icpc $(DEBUG) -o $(EXEC)-debug $(EXEC).cpp -lrt


# build the serial version of the program
$(EXEC)-serial: $(EXEC).cpp
	icpc $(OPT) -o $(EXEC)-serial -cilk-serialize $(EXEC).cpp -lrt

# build the optimized parallel version of the program
$(EXEC): $(EXEC).cpp
	icpc $(OPT) -o $(EXEC) $(EXEC).cpp -lrt

#run the optimized program in parallel
runp:
	@echo use make runp W=nworkers I=input_file
	$(XX) ./$(EXEC)  < $(I)

#run the serial version of your program
runs: $(EXEC)-serial
	@echo use make runs I=input_file 
	./$(EXEC)-serial < $(I)

#run the optimized program in with cilkscreen
screen: $(EXEC)
	cilkscreen ./$(EXEC) < screen_input

#run the optimized program in with cilkview
view: $(EXEC)
	cilkview ./$(EXEC) < $I


clean:
	/bin/rm -f $(OBJ) 
