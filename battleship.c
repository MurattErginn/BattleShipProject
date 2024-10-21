#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/time.h>

#define GRID_SIZE 4
#define SHIP_COUNT 4
#define PLAYER_COUNT 2

// The structure representing the player 
typedef struct {
    int grid[GRID_SIZE][GRID_SIZE];
    int hits;
    int attacked[GRID_SIZE][GRID_SIZE];
} Player;

typedef struct {
    Player players[PLAYER_COUNT];
    int game_over;    // Shared flag for checking if the game is over
    int winner;       // To store the winner (0 for Player 1, 1 for Player 2)
    int current_turn; // To track whose turn it is (0 for Player 1, 1 for Player 2)
} SharedState;

unsigned int getPreciseSeed() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned int)(tv.tv_sec ^ (tv.tv_usec * 1000) ^ getpid());
}

void initializeGrid(int grid[GRID_SIZE][GRID_SIZE], int attacked[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            grid[i][j] = 0;        // Zero for blank spaces.
            attacked[i][j] = 0;    // Zero for un-attacked, one for attacked.
        }
    }
}

void placeShips(int grid[GRID_SIZE][GRID_SIZE]) {
    int count = 0;
    while (count < SHIP_COUNT) {
        int row = rand() % GRID_SIZE;
        int column = rand() % GRID_SIZE;
        if (grid[row][column] == 0) {
            grid[row][column] = 1; // 1 for ships
            count++;
        }
    }
}

int checkHit(Player *attacker, Player *defender, int row, int column) {
    if (defender->grid[row][column] == 1) { // Check for ship
        defender->grid[row][column] = 0;    // Mark the hit
        attacker->hits++;                   // Increment the hit count for attacker
        return 1;                           // Hit
    } else {
        return 0;                           // Miss
    }
}

int checkWinner(Player *player) {
    return player->hits == SHIP_COUNT;
}

void takeTurn(Player *attacker, Player *defender, int player_number) {
    int row, column;
    do {
        row = rand() % GRID_SIZE;
        column = rand() % GRID_SIZE;
    } while (attacker->attacked[row][column]); // Keep picking until an un-attacked spot is found

    attacker->attacked[row][column] = 1; // Mark the spot as attacked

    printf("Player %d attacking coordinates: (%d, %d)\n", player_number, row, column);

    if (checkHit(attacker, defender, row, column)) {
        printf("Player %d: Hit!!\n", player_number);
    } else {
        printf("Player %d: Miss!!\n", player_number);
    }

    printf("------------------------------------------------------------\n");
}

int main() {
    srand(getPreciseSeed()); // Seed the random number generator

    // Create shared memory for the game state
    int shm_id = shmget(IPC_PRIVATE, sizeof(SharedState), IPC_CREAT | 0666);
    SharedState *state = (SharedState *)shmat(shm_id, NULL, 0);

    // Initialize players and game state
    for (int i = 0; i < PLAYER_COUNT; i++) {
        initializeGrid(state->players[i].grid, state->players[i].attacked);
        placeShips(state->players[i].grid);
        state->players[i].hits = 0;
    }
    state->game_over = 0;
    state->winner = -1;
    state->current_turn = 0; // Player 1 starts the game

    // Game loop
    pid_t pid = fork(); // Create a child process

    if (pid == 0) { // Child process (Player 2)
        srand(getPreciseSeed());
        while (!state->game_over) {
            sleep(1); // Ensure Player 1 takes its turn first
            if (state->current_turn == 1) { // Player 2's turn
                printf("Player 2's turn\n");
                takeTurn(&state->players[1], &state->players[0], 2); // Player 2 attacks Player 1
                if (checkWinner(&state->players[1])) {
                    state->game_over = 1;  // Game over
                    state->winner = 1;     // Player 2 wins
                    break;
                }
                state->current_turn = 0; // Switch turn to Player 1
            }
        }
    } else { // Parent process (Player 1)
        srand(getPreciseSeed());
        while (!state->game_over) {
            if (state->current_turn == 0) { // Player 1's turn
                printf("Player 1's turn\n");
                takeTurn(&state->players[0], &state->players[1], 1); // Player 1 attacks Player 2
                if (checkWinner(&state->players[0])) {
                    state->game_over = 1;  // Game over
                    state->winner = 0;     // Player 1 wins
                    break;
                }
                state->current_turn = 1; // Switch turn to Player 2
            }
            sleep(1); // Allow Player 2 to take its turn
        }

        // Wait for the child process (Player 2) to finish
        wait(NULL);

        // Announce the winner
        if (state->winner == 0) {
            printf("Player 1 wins!!\n");
        } else if (state->winner == 1) {
            printf("Player 2 wins!!\n");
        }
    }

    // Cleanup
    shmdt(state);
    shmctl(shm_id, IPC_RMID, NULL);

    return 0;
}

