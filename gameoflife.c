#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>

#define ROWS 60
#define COLS 60
#define OFF 0
#define ON 1
#define BLACK 8, 0, 0, 0, 0
#define scr_width 600
#define scr_height 600
#define cell_width (scr_width / ROWS)
#define cell_height (scr_height / COLS)

/** Note to self:
  * Always refer to the board as board[y][x] as to follow the general
  * standard for specifying coordinates. When looping, y must be declared
  * first in the outer loop so that it represents the rows while x, declared
  * within the y loop becomes the variable representing each column value.
 */

/** Bugs
  * = Severity -> Low
  *   - When ROWS or COLS is greater than the other, only a squared area
  *     is actually updated or a segfault is fired.
 */

/** We must hold two copies of the board so that we can analyze board and make
  * changes to temp when killing/creating cells so that we don't cause changes 
  * to the board to affect the following cells. 
 */

void randomize_board(void);
int num_neighbours(int x, int y);
void update_board(void);
void initialize_cells_array(void);

int iterations = 20;
int N = 100;
int max_y;

char **board;
char **temp;

int size;
int rank;

int main(int argc, char **argv) {
	int iterations_left = iterations;

	MPI_Init(&argc, &argv);

    /* Get communicator size */
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    /* Get process rank in the world communicator */
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	max_y = calc_max_y(rank);

board = malloc(N * sizeof(char*));
for (int i = 0; i < N; i++) {
  board[i] = malloc(max_y * sizeof(char));
}

temp = malloc(N * sizeof(char*));
for (int i = 0; i < N; i++) {
  temp[i] = malloc(max_y * sizeof(char));
}

    randomize_board();

	while (iterations_left > 0) {
		--iterations_left;
		notify_others();
		wait_for_others();
	}

	if (rank == 0) {
		char **whole_board = wait_for_all();

		pgmwrite(whole_board);
	} else {
		send_my_board();
	}
}

char **wait_for_all() {
	char[N][N] whole_board;

	char **his_board;
	MPI_Status	status;
	for (int i=0; i<size-1; ++i) {
		MPI_Recv(&his_board, ceil((double)N/size)*N, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD,
		      &status);
		int his_max_y = calc_max_y(status.MPI_SOURCE);
		int his_start_y = calc_start_y(status.MPI_SOURCE);

		merge_board(whole_board, his_board, his_start_y, his_start_y+his_max_y-2);
	}

	return whole_board;
}

void merge_board(char **big_board, char **small_board, int start_y, int end_y) {
	for (int y = 0; y < end_y - start_y; ++y) {
		for (int x = 0; x < N; ++x) {
			big_board[x][start_y+y] = small_board[y][x];
		}
	}
}

int calc_start_y(int rank) {
	int leftoverRows = N % size;

	int ranksWithLeftover = rank > leftoverRows ? leftoverRows : rank;
	int ranksWithoutLeftover = rank > leftoverRows ? rank - leftoverRows : 0;

	return ranksWithLeftover*(int)ceil((double)N/size) + 
			ranksWithoutLeftover*(int)floor((double)N/size); 
}

void send_my_board() {
	MPI_Bsend(&board[1][0], (max_y-2)*N, MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD);
}

void notify_others() {
	int prev_rank = (rank - 1 + size) % size;
	int next_rank = rank + 1 % size;

	MPI_Bsend(&board[2][0], N, MPI_CHAR, prev_rank, MPI_ANY_TAG, MPI_COMM_WORLD);
	MPI_Bsend(&board[max_y-1][0], N, MPI_CHAR, next_rank, MPI_ANY_TAG, MPI_COMM_WORLD);
}

void wait_for_others() {
	int prev_rank = (rank - 1 + size) % size;
	int next_rank = rank + 1 % size;

	MPI_Recv(&board[0][0], N, MPI_CHAR, prev_rank, MPI_ANY_TAG, MPI_COMM_WORLD);
	MPI_Recv(&board[max_y][0], N, MPI_CHAR, next_rank, MPI_ANY_TAG, MPI_COMM_WORLD);
}

int calc_max_y(int rank) {
	int leftoverRows = N % size;

	if (rank < leftoverRows) {
		return (int)ceil((double)N/size)+1;
	} else {
		return (int)floor((double)N/size)+1;
	}
}

void randomize_board(void) {
    for (int y = 1; y <= max_y-1; y++) {
        for (int x = 0; x < N; x++) {
            if (rand() % 5 == 0) {
                board[y][x] = ON;
            }
            temp[y][x] = board[y][x];
        }
    }
}

int num_neighbours(int x, int y) {
	int neighbours = 0;

	neighbours += is_cell_active(x+1, y);
	neighbours += is_cell_active(x-1, y);
	neighbours += is_cell_active(x, y+1);
	neighbours += is_cell_active(x, y-1);
	neighbours += is_cell_active(x+1, y+1);
	neighbours += is_cell_active(x-1, y-1);
	neighbours += is_cell_active(x+1, y-1);
	neighbours += is_cell_active(x-1, y+1);

	return neighbours;
}

int is_cell_active(int x, int y) {
	x = x % N;
	y = y % max_y;

	return board[y][x];
}

void update_board(void) {
    /** Detirmine which cells live and which die. Operate on temp so that
      * each change does not affect following changes, then copy temp into
      * the main board to be displayed.
     */
    int neighbours = 0;

    for (int y = 1; y <= max_y; y++) {
        for (int x = 0; x < N; x++) {
            neighbours = num_neighbours(x, y);
            if (neighbours < 2 && board[y][x] == ON) {
                temp[y][x] = OFF; /* Dies by underpopulation. */
            } else if (neighbours > 3 && board[y][x] == ON) {
                temp[y][x] = OFF; /* Dies by overpopulation. */
            } else if (neighbours == 3 && board[y][x] == OFF) {
                temp[y][x] = ON; /* Become alive because of reproduction. */
            }
            /* Otherwise the cell lives with just the right company. */
        }
    }

    for (int y = 1; y <= max_y; y++) {
        for (int x = 0; x < N; x++) {
            board[y][x] = temp[y][x];
        }
    }
}

void pgmwrite (char *filename, char **arr)
{
    FILE *fp;

	int tresh = 255;

    if (NULL == (fp = fopen(filename, "w")))
    {
		fprintf(stderr, "pgmwrite: cannot create <%s>\n", filename);
		exit(-1);
    }

    fprintf(fp, "P2\n");
    fprintf(fp, "# Written by pgmwrite\n");
    fprintf(fp, "%d %d\n", N, N);
    fprintf(fp, "%d\n", (int)thresh);

    for (int y = 0; y < N; y++) 
	{
		for (int x = 0; x < N; x++)
        {
			fprintf(fp, "%3d ", arr[y][x]*thresh);
        }
		fprintf(fp, "\n");
	}
   
   fclose(fp);
}
