#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cilk/cilk.h>
#include <cilk/reducer_max.h>
#include <cilk/reducer_opadd.h>
#include <cilk/reducer_vector.h>
#include <unistd.h>

#define BIT 0x1

#define X_BLACK 0
#define O_WHITE 1
#define GRANULARITY 3
#define OTHERCOLOR(c) (1-(c))

/* 
	represent game board squares as a 64-bit unsigned integer.
	these macros index from a row,column position on the board
	to a position and bit in a game board bitvector
*/
#define BOARD_BIT_INDEX(row,col) ((8 - (row)) * 8 + (8 - (col)))
#define BOARD_BIT(row,col) (0x1LL << BOARD_BIT_INDEX(row,col))
#define MOVE_TO_BOARD_BIT(m) BOARD_BIT(m.row, m.col)

/* all of the bits in the row 8 */
#define ROW8 \
  (BOARD_BIT(8,1) | BOARD_BIT(8,2) | BOARD_BIT(8,3) | BOARD_BIT(8,4) |	\
   BOARD_BIT(8,5) | BOARD_BIT(8,6) | BOARD_BIT(8,7) | BOARD_BIT(8,8))
			  
/* all of the bits in column 8 */
#define COL8 \
  (BOARD_BIT(1,8) | BOARD_BIT(2,8) | BOARD_BIT(3,8) | BOARD_BIT(4,8) |	\
   BOARD_BIT(5,8) | BOARD_BIT(6,8) | BOARD_BIT(7,8) | BOARD_BIT(8,8))

/* all of the bits in column 1 */
#define COL1 (COL8 << 7)

#define IS_MOVE_OFF_BOARD(m) (m.row < 1 || m.row > 8 || m.col < 1 || m.col > 8)
#define IS_DIAGONAL_MOVE(m) (m.row != 0 && m.col != 0)
#define MOVE_OFFSET_TO_BIT_OFFSET(m) (m.row * 8 + m.col)

typedef unsigned long long ull;

/* 
	game board represented as a pair of bit vectors: 
	- one for x_black disks on the board
	- one for o_white disks on the board
*/
typedef struct { ull disks[2]; } Board;

typedef struct { int row; int col; } Move;

typedef struct { Move move; int heuristic;} PossibleMove;

struct MaxValueWithIndex {
    int value;
    int index;

    MaxValueWithIndex(int idx = -1, int val = -100000) : value(val), index(idx) {}

    // Method to update the maximum value and index
    void update(int val, int idx) {
        if (val > value) {
            value = val;
            index = idx;
        }

    }

    // Comparison operator to determine which MaxValueWithIndex is greater
    bool operator<(const MaxValueWithIndex& other) const {
        return value < other.value;
    }
};


Board start = { 
	BOARD_BIT(4,5) | BOARD_BIT(5,4) /* X_BLACK */, 
	BOARD_BIT(4,4) | BOARD_BIT(5,5) /* O_WHITE */
};

Move offsets[] = {
  {0,1}		/* right */,		{0,-1}		/* left */, 
  {-1,0}	/* up */,		{1,0}		/* down */, 
  {-1,-1}	/* up-left */,		{-1,1}		/* up-right */, 
  {1,1}		/* down-right */,	{1,-1}		/* down-left */
};

int noffsets = sizeof(offsets)/sizeof(Move);
char diskcolor[] = { '.', 'X', 'O', 'I' };


void PrintDisk(int x_black, int o_white)
{
  printf(" %c", diskcolor[x_black + (o_white << 1)]);
}

void PrintBoardRow(int x_black, int o_white, int disks)
{
  /* Disks are the number of columns yet to be printed */
  if (disks > 1) {
    PrintBoardRow(x_black >> 1, o_white >> 1, disks - 1);
  }
  PrintDisk(x_black & BIT, o_white & BIT);
}

void PrintBoardRows(ull x_black, ull o_white, int rowsleft)
{
  if (rowsleft > 1) {
    /* Moving the board to the next row */
    PrintBoardRows(x_black >> 8, o_white >> 8, rowsleft - 1);
  }
  printf("%d", rowsleft);
  /* Current board row is at the end of the bits*/
  PrintBoardRow((int)(x_black & ROW8),  (int) (o_white & ROW8), 8);
  printf("\n");
}

void PrintBoard(Board b)
{
  printf("  1 2 3 4 5 6 7 8\n");
  PrintBoardRows(b.disks[X_BLACK], b.disks[O_WHITE], 8);
}

int CountBitsOnBoard(Board *b, int color)
{
  ull bits = b->disks[color];
  int ndisks = 0;
  for (; bits ; ndisks++) {
    bits &= bits - 1; // clear the least significant bit set
  }
  return ndisks;
}

/* 
	place a disk of color at the position specified by m.row and m,col,
	flipping the opponents disk there (if any) 
*/
void PlaceOrFlip(Move m, Board *b, int color) 
{
  ull bit = MOVE_TO_BOARD_BIT(m);
  b->disks[color] |= bit;
  b->disks[OTHERCOLOR(color)] &= ~bit;
}

/* 
	try to flip disks along a direction specified by a move offset.
	the return code is 0 if no flips were done.
	the return value is 1 + the number of flips otherwise.
*/
int TryFlips(Move m, Move offset, Board *b, int color, int verbose, int domove)
{
  Move next;
  next.row = m.row + offset.row;
  next.col = m.col + offset.col;

  if (!IS_MOVE_OFF_BOARD(next)) {
    ull nextbit = MOVE_TO_BOARD_BIT(next);
    if (nextbit & b->disks[OTHERCOLOR(color)]) {
      int nflips = TryFlips(next, offset, b, color, verbose, domove);
      if (nflips) {
        if (verbose) printf("flipping disk at %d,%d\n", next.row, next.col);
        if (domove) PlaceOrFlip(next,b,color);
        return nflips + 1;
      }
    } else if (nextbit & b->disks[color]) return 1;
  }
  return 0;
} 

int FlipDisks(Move m, Board *b, int color, int verbose, int domove)
{
  int nflips = 0;
	
  /* try flipping disks along each of the 8 directions */
  for(int i=0;i<noffsets;i++) {
    int flipresult = TryFlips(m,offsets[i], b, color, verbose, domove);
    // nflips += (flipresult > 0) ? flipresult - 1 : 0;
    if (flipresult > 0) {
      nflips += flipresult - 1;
    }
  }
  return nflips;
}

/*
 * Switches the standard input (stdin) to keyboard input if it is currently redirected from a file.
*/
void switchStdinToKeyboard() {
  // Check if stdin is not a terminal (e.g., redirected from a file)
  if (!isatty(fileno(stdin))) {
      // Reopen stdin from the terminal
      freopen("/dev/tty", "r", stdin);
      if (!stdin) {
          std::cerr << "Failed to redirect stdin to keyboard input." << std::endl;
          exit(EXIT_FAILURE);
      }
  }
}

void ReadMove(int color, Board *b)
{
  Move m;
  ull movebit;
  switchStdinToKeyboard();
  for(;;) {
    printf("Enter %c's move as 'row,col': ", diskcolor[color+1]);
    scanf("%d,%d",&m.row,&m.col);
    /* if move is not on the board, move again */
    if (IS_MOVE_OFF_BOARD(m)) {
      printf("Illegal move: row and column must both be between 1 and 8\n");
      PrintBoard(*b);
      continue;
    }
    movebit = MOVE_TO_BOARD_BIT(m);
		
    /* if board position occupied, move again */
    if (movebit & (b->disks[X_BLACK] | b->disks[O_WHITE])) {
      printf("Illegal move: board position already occupied.\n");
      PrintBoard(*b);
      continue;
    }
		
    /* if no disks have been flipped */ 
    {
      int nflips = FlipDisks(m, b,color, 1, 1);
      if (nflips == 0) {
        printf("Illegal move: no disks flipped\n");
        PrintBoard(*b);
        continue;
      }
      PlaceOrFlip(m, b, color);
      printf("You flipped %d disks\n", nflips);
      PrintBoard(*b);
    }
    break;
  }
}

/*
	return the set of board positions adjacent to an opponent's
	disk that are empty. these represent a candidate set of 
	positions for a move by color.
*/
Board NeighborMoves(Board b, int color)
{
  int i;
  Board neighbors = {0,0};
  for (i = 0;i < noffsets; i++) {
    ull colmask = (offsets[i].col != 0) ? 
      ((offsets[i].col > 0) ? COL1 : COL8) : 0;
    int offset = MOVE_OFFSET_TO_BIT_OFFSET(offsets[i]);

    if (offset > 0) {
      neighbors.disks[color] |= 
	(b.disks[OTHERCOLOR(color)] >> offset) & ~colmask;
    } else {
      neighbors.disks[color] |= 
	(b.disks[OTHERCOLOR(color)] << -offset) & ~colmask;
    }
  }
  neighbors.disks[color] &= ~(b.disks[X_BLACK] | b.disks[O_WHITE]);
  return neighbors;
}

/*
	return the set of board positions that represent legal
	moves for color. this is the set of empty board positions  
	that are adjacent to an opponent's disk where placing a
	disk of color will cause one or more of the opponent's
	disks to be flipped.
*/
int EnumerateLegalMoves(Board b, int color, Board *legal_moves)
{
  static Board no_legal_moves = {0,0};
  Board neighbors = NeighborMoves(b, color);
  ull my_neighbor_moves = neighbors.disks[color];
  int row;
  int col;
	
  int num_moves = 0;
  *legal_moves = no_legal_moves;
	
  for(row=8; row >=1; row--) {
    ull thisrow = my_neighbor_moves & ROW8;
    for(col=8; thisrow && (col >= 1); col--) {
      if (thisrow & COL8) {
        Move m = { row, col };
        if (FlipDisks(m, &b, color, 0, 0) > 0) {
          legal_moves->disks[color] |= BOARD_BIT(row,col);
          num_moves++;
        }
      }
      thisrow >>= 1;
    }
    my_neighbor_moves >>= 8;
  }
  return num_moves;
}


std::vector<PossibleMove> ReturnLegalMoves(Board b, int color){
  std::vector<PossibleMove> moves;
  Board neighbors = NeighborMoves(b, color);
  ull my_neighbor_moves = neighbors.disks[color];
  int row;
  int col;
	
  for(row=8; row >=1; row--) {
    ull thisrow = my_neighbor_moves & ROW8;
    for(col=8; thisrow && (col >= 1); col--) {
      if (thisrow & COL8) {
        Move m = { row, col };
        if (FlipDisks(m, &b, color, 0, 0) > 0) {
          PossibleMove nextMove = {m, -1};
          moves.push_back(nextMove);
        }
      }
      thisrow >>= 1;
    }
    my_neighbor_moves >>= 8;
  }
  return moves;
}

int HumanTurn(Board *b, int color)
{
  Board legal_moves;
  int num_moves = EnumerateLegalMoves(*b, color, &legal_moves);
  if (num_moves > 0) {
    ReadMove(color, b);
    return 1;
  } else return 0;
}

int CalculateHeuristic(Board *b, int color) {
  return CountBitsOnBoard(b, color) - CountBitsOnBoard(b, OTHERCOLOR(color));
}

int MakeMove(Move m, Board *b, int color) {
      int nflips = FlipDisks(m, b,color, 0, 1);
      PlaceOrFlip(m, b, color);
      return nflips;
}

PossibleMove SequentialSearch(Board *b, int color, int depth) {
  PossibleMove bestMove;
  if (depth == 0) {
    bestMove.move.col = -1;
    bestMove.move.row = -1;
    bestMove.heuristic = CalculateHeuristic(b, color);
    return bestMove;
  } else {
    // check for all valid moves and store them in a vector
    std::vector<PossibleMove> moves;
    moves = ReturnLegalMoves(*b, color);

    // iterate through valid moves and check the heuristic to find the best move
    int i;
    bestMove.heuristic = -1000000;
    for (i = 0; i < moves.size(); i++) {
      Board copyBoard = *b;
      PossibleMove m = moves[i];
      // make a move
      MakeMove(m.move, &copyBoard, color);

      // update bestMove if needed
      int currHeuristic = - SequentialSearch(&copyBoard, OTHERCOLOR(color), depth - 1).heuristic;
      if (currHeuristic > bestMove.heuristic) {
        bestMove = moves[i];
        bestMove.heuristic = currHeuristic;
      }
    }

    // Handle case where there are no valid moves
    if (moves.size() == 0) {
      Board legal_moves;
      if (EnumerateLegalMoves(*b, OTHERCOLOR(color), &legal_moves) == 0) {
        bestMove.heuristic = CalculateHeuristic(b, color);
      } else {
        bestMove = SequentialSearch(b, OTHERCOLOR(color), depth);
        bestMove.heuristic = -bestMove.heuristic;
      }
    }
    return bestMove;
  }
  
}

PossibleMove ParallelSearch(Board *b, int color, int depth) {
  if (depth < GRANULARITY) {
    return SequentialSearch(b, color, depth);
  } else {
    // Check for all valid moves and store them in a vector
    std::vector<PossibleMove> moves;
    PossibleMove bestMove;
    moves = ReturnLegalMoves(*b, color);

    // Reducer to find best move without data races and conflicts
    cilk::reducer<cilk::op_max<MaxValueWithIndex>> max_reducer;
    cilk_for(int i = 0; i < moves.size(); i++) {
      Board copyBoard = *b;
      PossibleMove m = moves[i];
      // Make a move
      MakeMove(m.move, &copyBoard, color);

      // Update maximum score
      max_reducer->calc_max(MaxValueWithIndex(i, - ParallelSearch(&copyBoard, OTHERCOLOR(color), depth - 1).heuristic));
    }

    // Handle case where there are no valid moves
    if (moves.size() == 0) {
      Board legal_moves;
      if (EnumerateLegalMoves(*b, OTHERCOLOR(color), &legal_moves) == 0) {
        bestMove.heuristic = CalculateHeuristic(b, color);
      } else {
        bestMove = ParallelSearch(b, OTHERCOLOR(color), depth);
        bestMove.heuristic = -bestMove.heuristic;
      }
    } else {
      MaxValueWithIndex result = max_reducer.get_value();
      bestMove = moves[result.index];
      bestMove.heuristic = result.value;
    }
    return bestMove;
  }
}

// Does the move with highest heuristic for the given search depth
PossibleMove ComputerMove(Board *b, int color, int depth) {
  PossibleMove bestMove;
  // Negamax algorithm
  bestMove = ParallelSearch(b, color, depth);
  return bestMove;

}

int ComputerTurn(Board *b, int color, int depth) {
  PossibleMove bestMove;
  Board legal_moves;
  int num_moves = EnumerateLegalMoves(*b, color, &legal_moves);
  if (num_moves > 0) {
    // find the best move
    bestMove = ComputerMove(b, color, depth);
    // do the move
    int nflips = MakeMove(bestMove.move, b, color);
    // print results
    printf("Computer move is %c at %d, %d\n", diskcolor[color+1], bestMove.move.row, bestMove.move.col);
    printf("Computer flipped %d disks\n", nflips);
    PrintBoard(*b);
    return 1;
  }
  return 0;


}

void EndGame(Board b)
{
  int o_score = CountBitsOnBoard(&b,O_WHITE);
  int x_score = CountBitsOnBoard(&b,X_BLACK);
  printf("Game over. \n");
  if (o_score == x_score)  {
    printf("Tie game. Each player has %d disks\n", o_score);
  } else { 
    printf("X has %d disks. O has %d disks. %c wins.\n", x_score, o_score, 
	      (x_score > o_score ? 'X' : 'O'));
  }
}

int main (int argc, const char * argv[]) 
{
  Board gameboard = start;
  int move_possible;
  char c1;
  char c2;
  int n1;
  int n2;

  printf("Enter h if player 1 is a human player, c as a computer player:");
  scanf(" %c", &c1);
  if (c1 == 'c')
  {
    printf("Enter the searching depth between 1 and 60: ");
    scanf("%d", &n1);
  }

  printf("Enter h if player 2 is a human player, c as a computer player:");
  scanf(" %c", &c2);
  if (c2 == 'c')
  {
    printf("Enter the searching depth between 1 and 60: ");
    scanf("%d", &n2);
  }
  printf("Player 1 is %c with depth %d\nPlayer 2 is %c with depth %d\n", c1, n1, c2, n2);

  // player 1 is computer player 2 is human player
  // TODO fix infinite loop when human player makes illegal move
  if (c1 == 'c' && c2 == 'h') {
    do {
      move_possible = ComputerTurn(&gameboard, X_BLACK, n1) | HumanTurn(&gameboard, O_WHITE);
    } while(move_possible);
  }

  // player 1 is human player 2 is computer player
  else if (c1 == 'h' && c2 == 'c') {
    do {
      move_possible = HumanTurn(&gameboard, X_BLACK) | ComputerTurn(&gameboard, O_WHITE, n2);
    } while(move_possible);
  }
  
  // both players are human players
  else if (c1 == 'h' && c2 == 'h') {
    do {
      move_possible = 
        HumanTurn(&gameboard, X_BLACK) | 
        HumanTurn(&gameboard, O_WHITE);
    } while(move_possible);
  }

  // both players are computer players
  else if (c1 == 'c' && c2 == 'c') {
    do {
      move_possible = 
        ComputerTurn(&gameboard, X_BLACK, n1) | 
        ComputerTurn(&gameboard, O_WHITE, n2);
    } while(move_possible);
  }
  // Game is over, compute final score
  EndGame(gameboard);
	
  return 0;
}
