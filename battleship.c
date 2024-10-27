#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ncurses.h>

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

void initializeNcurses() {
    initscr();             
    cbreak();              
    noecho();              
    keypad(stdscr, TRUE);  
    curs_set(0);           
    refresh();
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

void displayJudgeView(Player *p1, Player *p2, int player) {
    mvprintw(0, 0, "Judge's View:");
    mvprintw(1, 0, "        Player 1         |         Player 2");

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            char p1_symbol = (p1->attacked[i][j] == 2) ? 'X' :
                             (p1->attacked[i][j] == -1) ? 'O' :
                             (p1->grid[i][j] == 1 ? 'S' : '.');
            mvprintw(i + 2, j * 3, " %c ", p1_symbol);
        }

        mvprintw(i + 2, GRID_SIZE * 3, " | ");  

        for (int j = 0; j < GRID_SIZE; j++) {
            char p2_symbol = (p2->attacked[i][j] == 2) ? 'X' :
                             (p2->attacked[i][j] == -1) ? 'O' :
                             (p2->grid[i][j] == 1 ? 'S' : '.');
            mvprintw(i + 2, GRID_SIZE * 3 + 3 + j * 3, " %c ", p2_symbol);
        }
    }

    if(player == 0)
        displayGridNCurses(p1->attacked, "  AI 1's View", GRID_SIZE + 4, 0);
    else if(player == 1)
        displayGridNCurses(p2->attacked, "  AI 2's View", GRID_SIZE + 4, 0);

    refresh(); 
}

void displayGridNCurses(int grid[GRID_SIZE][GRID_SIZE], const char *title, int startY, int startX) {
    mvprintw(startY, startX, "%s:", title); 

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            char symbol = (grid[i][j] == -1) ? 'O' : 
                          (grid[i][j] == 2) ? 'X' : '.';
            mvprintw(startY + i + 1, startX + j * 2, " %c ", symbol);
        }
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
void aiTurn(SharedState *state, int ai_id) {
    Player *ai = &state->players[ai_id];
    Player *opponent = &state->players[1 - ai_id];
    int row, col;

    int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    
    if (state->ai_targeting[ai_id]) {
        // AI is in targeting mode
        for (int i = 0; i < 4; i++) {
            row = state->ai_last_hit_row[ai_id] + directions[i][0];
            col = state->ai_last_hit_col[ai_id] + directions[i][1];

            if (row >= 0 && row < GRID_SIZE && col >= 0 && col < GRID_SIZE &&
                ai->attacked[row][col] == 0) {
                mvprintw(2 * GRID_SIZE + 6, 0, "AI %d targets: (%d, %d)\n", ai_id + 1, row, col);

                if (checkHit(ai, opponent, row, col)) {
                    mvprintw(2 * GRID_SIZE + 7, 0, "AI %d Hit!\n", ai_id + 1);
                    state->ai_last_hit_row[ai_id] = row;
                    state->ai_last_hit_col[ai_id] = col;
                    return;
                } else {
                    mvprintw(2 * GRID_SIZE + 7, 0, "AI %d Miss.\n", ai_id + 1);
                    state->ai_targeting[ai_id] = 0;
                    break;
                }
            }
        }
    }

    int foundUnattacked = 0;
    for (int i = 0; i < GRID_SIZE && !foundUnattacked; i++) {
        for (int j = (i % 2); j < GRID_SIZE; j += 2) {
            if (ai->attacked[i][j] == 0) {
                row = i;
                col = j;
                foundUnattacked = 1;
                break;
            }
        }
    }
    if (!foundUnattacked) {
        do {
            row = rand() % GRID_SIZE;
            col = rand() % GRID_SIZE;
        } while (ai->attacked[row][col] != 0);
    }

    mvprintw(2 * GRID_SIZE + 8, 0, "AI %d hunts: (%d, %d)\n", ai_id + 1, row, col);
    
    if (checkHit(ai, opponent, row, col)) {
        mvprintw(2 * GRID_SIZE + 7, 0, "AI %d Hits!\n", ai_id + 1);
        state->ai_last_hit_row[ai_id] = row;
        state->ai_last_hit_col[ai_id] = col;
        state->ai_targeting[ai_id] = 1;
    } else {
        mvprintw(2 * GRID_SIZE + 7, 0, "AI %d Misses.\n", ai_id + 1);
    }
}

void gameLoop(SharedState *state) { // Game Loop
    while (!state->game_over) {
    	saveGameState(state);
    
        clear();

        if (state->current_turn == 0) {
            aiTurn(state, 0);
            displayJudgeView(&state->players[0], &state->players[1], 0);

            if (checkWinner(&state->players[0])) {
                state->game_over = 1;
                state->winner = 0;
            } else {
                state->current_turn = 1;
            }
        } else {
            aiTurn(state, 1);
            displayJudgeView(&state->players[0], &state->players[1], 1);
            if (checkWinner(&state->players[1])) {
                state->game_over = 1;
                state->winner = 1;
            } else {
                state->current_turn = 0;
            }
        }
        sleep(1);
    }

    mvprintw(GRID_SIZE + 5, 0, "AI %d won!\n", state->winner + 1);
    sleep(1);
    refresh();

    sleep(5);
    refresh();
}

void displayMenu(SharedState *state) {
    int choice;
    while (1) {
        clear();
        mvprintw(0, 0, "--- Battleship Menu ---");
        mvprintw(1, 0, "1. Start New Game");
        mvprintw(2, 0, "2. Load Saved Game");
        mvprintw(3, 0, "3. Display Grids");
        mvprintw(4, 0, "4. Re-locate Ships");
        mvprintw(5, 0, "5. Exit");
        mvprintw(7, 0, "Enter your choice: ");
        refresh();
        choice = getch();

        switch (choice) {
            case '1':
                initializeGame(state);
                gameLoop(state);
                break;
            case '2':
                if (loadGameState(state)) {
                    gameLoop(state);
                } else {
                    mvprintw(9, 0, "No saved game found. Starting new game.");
                    refresh();
                    sleep(2);
                    initializeGame(state);
                    gameLoop(state);
                }
                break;
            case '3':
                clear();
                displayJudgeView(&state->players[0], &state->players[1]);
                mvprintw(GRID_SIZE + 5, 0, "Press any key to return to menu...");
                refresh();
                getch();
                break;
            case '4':
                for (int i = 0; i < PLAYER_COUNT; i++) {
                    initializeGrid(state->players[i].grid, state->players[i].attacked);
                    automaticPlacement(state->players[i].grid);
                }
                mvprintw(9, 0, "Ships re-located.");
                refresh();
                sleep(2);
                break;
            case '5':
                endwin();
                exit(0);
            default:
                mvprintw(9, 0, "Invalid choice. Please try again.");
                refresh();
                sleep(1);
                break;
        }
    }
}

void initializeGame(SharedState *state) {
    state->current_turn = 0;
    state->game_over = 0;
    state->winner = -1;

    for (int i = 0; i < PLAYER_COUNT; i++) {
        initializeGrid(state->players[i].grid, state->players[i].attacked);
        automaticPlacement(state->players[i].grid);
        state->players[i].hits = 0;
        state->ai_targeting[i] = 0;
    }
}

int main() {
    srand(getPreciseSeed());

    // ncurses start
    initscr();
    cbreak();
    noecho();
    curs_set(FALSE);

    int shm_id = shmget(IPC_PRIVATE, sizeof(SharedState), IPC_CREAT | 0666);
    SharedState *state = (SharedState *)shmat(shm_id, NULL, 0);

    displayMenu(state);

    shmdt(state);
    shmctl(shm_id, IPC_RMID, NULL);

    // ncurses end
    endwin();
    return 0;
}

void saveGameState(SharedState *state) {
    FILE *file = fopen("battleship_save.dat", "wb");
    if (file == NULL) {
        perror("Failed to save game state");
        return;
    }
    fwrite(state, sizeof(SharedState), 1, file);
    fclose(file);
    printf("Game state saved.\n");
}

int loadGameState(SharedState *state) {
    FILE *file = fopen("battleship_save.dat", "rb");
    if (file == NULL) {
        printf("No saved game found. Starting new game.\n");
        return 0;
    }
    fread(state, sizeof(SharedState), 1, file);
    fclose(file);
    return 1;
}
