#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <SDL/SDL.h>
#include <mpi.h>

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
char **wait_for_all();

int iterations = 20;
int N = 3000;
int max_y;

char **board;
char **temp;

int size;
int rank;


char **malloc_2d_array(int cols, int rows) {
	char *data = (char *)malloc(rows*cols*sizeof(char));
	char **arr = (char **)malloc(rows*sizeof(char*));
	int i;
	for (i = 0; i<rows; i++)
		arr[i] = &(data[cols*i]);

	return arr;
}

int main(int argc, char **argv) {
	int iterations_left = iterations;

	MPI_Init(&argc, &argv);

    /* Get communicator size */
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    /* Get process rank in the world communicator */
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	srand(time(NULL)^rank*size);
	double start_time = MPI_Wtime();

	max_y = calc_max_y(rank);

	board = malloc_2d_array(N, max_y+1);
	temp = malloc_2d_array(N, max_y+1);

    	randomize_board();
	notify_others();
	wait_for_others();

	while (iterations_left > 0) {
		--iterations_left;
		update_board();
		notify_others();
		wait_for_others();
	}

	if (rank == 0) {
		int start_y = calc_start_y(rank);

		char **whole_board = wait_for_all();

		merge_board(whole_board, board, start_y, start_y + max_y-2);

		pgmwrite("result.pgm", whole_board);
	} else {
		send_my_board();
	}

	double end_time = MPI_Wtime();

	if (rank == 0) {
		printf("Time taken: %f\n", end_time - start_time);
	}

	MPI_Finalize();
}

char **wait_for_all() {
	char** whole_board = malloc_2d_array(N, N);

	char **his_board = malloc_2d_array(N, ceil((double)N / size));
	MPI_Status	status;
	int i;
	for (i=0; i<size-1; ++i) {
		MPI_Recv(&(his_board[0][0]), ceil((double)N/size)*N, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD,
		      &status);
		int his_max_y = calc_max_y(status.MPI_SOURCE);
		int his_start_y = calc_start_y(status.MPI_SOURCE);

		int start_y = his_start_y;
		int end_y = his_start_y + his_max_y - 2;

		merge_board(whole_board, his_board, start_y, end_y);
	}

	return whole_board;
}

void merge_board(char **big_board, char **small_board, int start_y, int end_y) {
	int y;
	int x;
	for (y = 0; y <= end_y - start_y; ++y) {
		for (x = 0; x < N; ++x) {
			big_board[start_y + y][x] = small_board[y][x];
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
	MPI_Send(&board[1][0], (max_y-1)*N, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
}

void notify_others() {
	int prev_rank = (rank - 1 + size) % size;
	int next_rank = (rank + 1) % size;

	MPI_Request request;
	MPI_Isend(&(board[1][0]), N, MPI_CHAR, prev_rank, 0, MPI_COMM_WORLD,
	&request);
	MPI_Isend(&(board[max_y-1][0]), N, MPI_CHAR, next_rank, 0, MPI_COMM_WORLD, &request);
}

void wait_for_others() {
	int prev_rank = (rank - 1 + size) % size;
	int next_rank = (rank + 1) % size;

	MPI_Recv(&board[0][0], N, MPI_CHAR, prev_rank, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	MPI_Recv(&board[max_y][0], N, MPI_CHAR, next_rank, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
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
	int y;
	int x;
    for (y = 1; y <= max_y-1; y++) {
        for (x = 0; x < N; x++) {
            if (rand() % 5 == 0) {
                board[y][x] = ON;
			}
			else 
			{
				board[y][x] = OFF;
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

	int y;
	int x;

    for (y = 1; y <= max_y; y++) {
        for (x = 0; x < N; x++) {
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

    for (y = 1; y <= max_y; y++) {
        for (x = 0; x < N; x++) {
            board[y][x] = temp[y][x];
        }
    }
}

void pgmwrite (char *filename, char **arr)
{
    FILE *fp;

	int thresh = 255;

    if (NULL == (fp = fopen(filename, "w")))
    {
		fprintf(stderr, "pgmwrite: cannot create <%s>\n", filename);
		exit(-1);
    }

    fprintf(fp, "P2\n");
    fprintf(fp, "# Written by pgmwrite\n");
    fprintf(fp, "%d %d\n", N, N);
    fprintf(fp, "%d\n", (int)thresh);

	int y;
	int x;

    for (y = 0; y < N; y++) 
	{
		for (x = 0; x < N; x++)
        {
			fprintf(fp, "%3d ", arr[y][x] * thresh);
		}
		fprintf(fp, "\n");
	}
   
   fclose(fp);
}
