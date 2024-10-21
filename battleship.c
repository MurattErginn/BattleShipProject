#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define GRID_SIZE 8
#define SHIP_COUNT 5
#define PLAYER_COUNT 2

int ship_sizes[SHIP_COUNT] = {4, 3, 3, 2, 2};

typedef struct {
    int grid[GRID_SIZE][GRID_SIZE];      // Actual grid with ships
    int attacked[GRID_SIZE][GRID_SIZE];  // Track attacks: -1 = Miss, 2 = Hit
    int hits;                            // Total hits
} Player;

typedef struct {
    Player players[PLAYER_COUNT];
    int current_turn;
    int game_over;
    int winner;
    int ai_last_hit_row[PLAYER_COUNT];
    int ai_last_hit_col[PLAYER_COUNT];
    int ai_targeting[PLAYER_COUNT];
} SharedState;

unsigned int getPreciseSeed() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned int)(tv.tv_sec ^ (tv.tv_usec * 1000) ^ getpid());
}

void initializeGrid(int grid[GRID_SIZE][GRID_SIZE], int attacked[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            grid[i][j] = 0;
            attacked[i][j] = 0;
        }
    }
}

int canPlaceShip(int grid[GRID_SIZE][GRID_SIZE], int row, int col, int size, int horizontal) {
    for (int i = 0; i < size; i++) {
        int r = row + (horizontal ? 0 : i);
        int c = col + (horizontal ? i : 0);

        if (r < 0 || r >= GRID_SIZE || c < 0 || c >= GRID_SIZE || grid[r][c] != 0) {
            return 0;
        }
    }
    return 1;
}

void placeShip(int grid[GRID_SIZE][GRID_SIZE], int size) {
    int placed = 0;
    while (!placed) {
        int row = rand() % GRID_SIZE;
        int col = rand() % GRID_SIZE;
        int horizontal = rand() % 2;

        if (canPlaceShip(grid, row, col, size, horizontal)) {
            for (int i = 0; i < size; i++) {
                grid[row + (horizontal ? 0 : i)][col + (horizontal ? i : 0)] = 1;
            }
            placed = 1;
        }
    }
}

void automaticPlacement(int grid[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < SHIP_COUNT; i++) {
        placeShip(grid, ship_sizes[i]);
    }
}

void displayGrid(int grid[GRID_SIZE][GRID_SIZE], const char *title) {
    printf("\n%s:\n", title);
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (grid[i][j] == -1) {
                printf(" O ");  // Miss
            } else if (grid[i][j] == 2) {
                printf(" X ");  // Hit
            } else {
                printf(" . ");  // Unattacked
            }
        }
        printf("\n");
    }
}

void displayJudgeView(Player *p1, Player *p2) {
    printf("\nJudge's View:\n");
    printf("Player 1 | Player 2\n");
    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            if (p1->attacked[i][j] == 2) {
                printf(" X ");  // Hit
            } else if (p1->attacked[i][j] == -1) {
                printf(" O ");  // Miss
            } else {
                printf(" %c ", p1->grid[i][j] == 1 ? 'S' : '.');  // Ship or Empty
            }
        }
        printf("   ");
        for (int j = 0; j < GRID_SIZE; j++) {
            if (p2->attacked[i][j] == 2) {
                printf(" X ");  // Hit
            } else if (p2->attacked[i][j] == -1) {
                printf(" O ");  // Miss
            } else {
                printf(" %c ", p2->grid[i][j] == 1 ? 'S' : '.');  // Ship or Empty
            }
        }
        printf("\n");
    }
}

int checkHit(Player *attacker, Player *defender, int row, int col) {
    if (defender->grid[row][col] == 1) {
        defender->grid[row][col] = 0;
        attacker->attacked[row][col] = 2;
        attacker->hits++;
        return 1;
    } else {
        attacker->attacked[row][col] = -1;
        return 0;
    }
}

int checkWinner(Player *player) {
    return player->hits == 14;
}

void aiTurn(SharedState *state, int ai_id) { // AI Targeting is here.
    Player *ai = &state->players[ai_id];
    Player *opponent = &state->players[1 - ai_id];
    int row, col;

    if (state->ai_targeting[ai_id]) {
        int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (int i = 0; i < 4; i++) {
            row = state->ai_last_hit_row[ai_id] + directions[i][0];
            col = state->ai_last_hit_col[ai_id] + directions[i][1];

            if (row >= 0 && row < GRID_SIZE && col >= 0 && col < GRID_SIZE &&
                ai->attacked[row][col] == 0) {
                printf("AI %d targets: (%d, %d)\n", ai_id + 1, row, col);
                if (checkHit(ai, opponent, row, col)) {
                    printf("AI %d: Hit!\n", ai_id + 1);
                    state->ai_last_hit_row[ai_id] = row;
                    state->ai_last_hit_col[ai_id] = col;
                } else {
                    printf("AI %d: Miss.\n", ai_id + 1);
                }
                return;
            }
        }
        state->ai_targeting[ai_id] = 0;
    }

    do {
        row = rand() % GRID_SIZE;
        col = rand() % GRID_SIZE;
    } while (ai->attacked[row][col] != 0);

    printf("AI %d hunts: (%d, %d)\n", ai_id + 1, row, col);
    if (checkHit(ai, opponent, row, col)) {
        printf("AI %d: Hit!\n", ai_id + 1);
        state->ai_last_hit_row[ai_id] = row;
        state->ai_last_hit_col[ai_id] = col;
        state->ai_targeting[ai_id] = 1;
    } else {
        printf("AI %d: Miss.\n", ai_id + 1);
    }
}

void gameLoop(SharedState *state) { // Game Loop
    while (!state->game_over) {
        if (state->current_turn == 0) {
            aiTurn(state, 0);
            displayGrid(state->players[0].attacked, "AI 1's View");
            displayJudgeView(&state->players[0], &state->players[1]);
            if (checkWinner(&state->players[0])) {
                state->game_over = 1;
                state->winner = 0;
            } else {
                state->current_turn = 1;
            }
        } else {
            aiTurn(state, 1);
            displayGrid(state->players[1].attacked, "AI 2's View");
            displayJudgeView(&state->players[0], &state->players[1]);
            if (checkWinner(&state->players[1])) {
                state->game_over = 1;
                state->winner = 1;
            } else {
                state->current_turn = 0;
            }
        }
        sleep(1);
    }

    printf("AI %d wins!\n", state->winner + 1);
}

int main() { // Need to add the menu.
    srand(getPreciseSeed());

    int shm_id = shmget(IPC_PRIVATE, sizeof(SharedState), IPC_CREAT | 0666);
    SharedState *state = (SharedState *)shmat(shm_id, NULL, 0);

    for (int i = 0; i < PLAYER_COUNT; i++) {
        initializeGrid(state->players[i].grid, state->players[i].attacked);
        automaticPlacement(state->players[i].grid);
        state->players[i].hits = 0;
    }

    state->current_turn = 0;
    state->game_over = 0;

    gameLoop(state);

    shmdt(state);
    shmctl(shm_id, IPC_RMID, NULL);

    return 0;
}
