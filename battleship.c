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
    int grid[GRID_SIZE][GRID_SIZE];      // Gemilerin bulunduğu grid
    int attacked[GRID_SIZE][GRID_SIZE];  // Saldırı takibi: -1 = Iskala, 2 = Vuruş
    int hits;                            // Toplam vuruşlar
} Player;


typedef struct {
    Player players[PLAYER_COUNT];
    int current_turn;
    int game_over;
    int winner;

    int ai_last_hit_row[PLAYER_COUNT];
    int ai_last_hit_col[PLAYER_COUNT];
    int ai_first_hit_row[PLAYER_COUNT];
    int ai_first_hit_col[PLAYER_COUNT];
    int ai_targeting[PLAYER_COUNT];
    int ai_direction[PLAYER_COUNT];
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

int isCellEmptyWithGap(int grid[GRID_SIZE][GRID_SIZE], int row, int col) {
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            int r = row + i;
            int c = col + j;
            if (r >= 0 && r < GRID_SIZE && c >= 0 && c < GRID_SIZE && grid[r][c] != 0) {
                return 0; // Adjacent cell is occupied
            }
        }
    }
    return 1; // No adjacent cells are occupied
}


int canPlaceShip(int grid[GRID_SIZE][GRID_SIZE], int row, int col, int size, int horizontal) {
    for (int i = 0; i < size; i++) {
        int r = row + (horizontal ? 0 : i);
        int c = col + (horizontal ? i : 0);

        if (r < 0 || r >= GRID_SIZE || c < 0 || c >= GRID_SIZE || grid[r][c] != 0 || !isCellEmptyWithGap(grid, r, c)) {
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
    mvprintw(0, 0, "                   Judge's View:");
    mvprintw(1, 0, "        Player 1         |         Player 2");

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            char p1_symbol = (p2->attacked[i][j] == 2) ? 'X' :
                             (p2->attacked[i][j] == -1) ? '.' :
                             (p1->grid[i][j] == 1 ? 'S' : '.');
            mvprintw(i + 2, j * 3, " %c ", p1_symbol);
        }

        mvprintw(i + 2, GRID_SIZE * 3, " | ");  

        for (int j = 0; j < GRID_SIZE; j++) {
            char p2_symbol = (p1->attacked[i][j] == 2) ? 'X' :
                             (p1->attacked[i][j] == -1) ? '.' :
                             (p2->grid[i][j] == 1 ? 'S' : '.');
            mvprintw(i + 2, GRID_SIZE * 3 + 3 + j * 3, " %c ", p2_symbol);
        }
    }

    displayGridNCurses(p1->attacked, p2->attacked, GRID_SIZE + 4);

    refresh(); 
}

void displayGridNCurses(Player *p1, Player *p2, int startY) {
    mvprintw(startY, 0, "       AI 1's View       |       AI 2's View");

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            char p1_symbol = (p1->grid[i][j] == -1) ? 'O' : 
                             (p1->grid[i][j] == 2) ? 'X' : '.';
            mvprintw(startY + i + 1, j * 3, " %c ", p1_symbol);
        }

        mvprintw(startY + i + 1, GRID_SIZE * 3, " | ");  

        for (int j = 0; j < GRID_SIZE; j++) {
            char p2_symbol = (p2->grid[i][j] == -1) ? 'O' : 
                             (p2->grid[i][j] == 2) ? 'X' : '.';
            mvprintw(startY + i + 1, GRID_SIZE * 3 + 3 + j * 3, " %c ", p2_symbol);
        }
    }
}    

int checkHit(Player *attacker, Player *defender, int row, int col) {
    if (defender->grid[row][col] > 0) {
        attacker->attacked[row][col] = 2;  // Vuruşu işaretle
        defender->hits++;
        
        for (int r = 0; r < GRID_SIZE; r++) {
            for (int c = 0; c < GRID_SIZE; c++) {
                if (defender->grid[r][c] == defender->grid[row][col] && attacker->attacked[r][c] == 2) {
                }
            }
        }

        return 1;  // Vuruş başarılı
    } else {
        attacker->attacked[row][col] = -1;  // Iskala
        return 0;  // Vuruş başarısız
    }
}


int checkWinner(Player *player) {
    return player->hits == 14;
}

void aiTurn(SharedState *state, int ai_id) {
    Player *ai = &state->players[ai_id];
    Player *opponent = &state->players[1 - ai_id];
    int row, col;

    if (state->ai_targeting[ai_id]) {
        int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

        if (state->ai_direction[ai_id] == -1) {
            for (int i = 0; i < 4; i++) {
                row = state->ai_first_hit_row[ai_id] + directions[i][0];
                col = state->ai_first_hit_col[ai_id] + directions[i][1];

                if (row >= 0 && row < GRID_SIZE && col >= 0 && col < GRID_SIZE &&
                    ai->attacked[row][col] == 0) {
                    if (checkHit(ai, opponent, row, col)) {
                        state->ai_last_hit_row[ai_id] = row;
                        state->ai_last_hit_col[ai_id] = col;
                        state->ai_direction[ai_id] = i;  // Yönü kaydet
                    }
                    return;
                }
            }
            state->ai_targeting[ai_id] = 0;
        } else {
            // Belirli doğrultuda ilerle
            int dir = state->ai_direction[ai_id];
            row = state->ai_last_hit_row[ai_id] + directions[dir][0];
            col = state->ai_last_hit_col[ai_id] + directions[dir][1];

            if (row >= 0 && row < GRID_SIZE && col >= 0 && 
            col < GRID_SIZE && ai->attacked[row][col] == 0) {
                if (checkHit(ai, opponent, row, col)) {
                    state->ai_last_hit_row[ai_id] = row;
                } else {
                    int opposite_dir = (dir % 2 == 0) ? dir + 1 : dir - 1;
                    state->ai_last_hit_row[ai_id] = state->ai_first_hit_row[ai_id];
                    state->ai_last_hit_col[ai_id] = state->ai_first_hit_col[ai_id];
                    state->ai_direction[ai_id] = opposite_dir;
                }
                return;
            }
            // Eğer ilerleyemiyorsak diğer tarafa geç
            state->ai_last_hit_row[ai_id] = state->ai_first_hit_row[ai_id];
            state->ai_last_hit_col[ai_id] = state->ai_first_hit_col[ai_id];
            state->ai_direction[ai_id] = (dir % 2 == 0) ? dir + 1 : dir - 1;
        }
    }

    // Avlanma moduna geç
    do {
        row = rand() % GRID_SIZE;
        col = rand() % GRID_SIZE;
    } while (ai->attacked[row][col] != 0);

    if (checkHit(ai, opponent, row, col)) {
        state->ai_first_hit_row[ai_id] = row;
        state->ai_first_hit_col[ai_id] = col;
        state->ai_last_hit_row[ai_id] = row;
        state->ai_last_hit_col[ai_id] = col;
        state->ai_targeting[ai_id] = 1;
        state->ai_direction[ai_id] = -1;
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

    mvprintw(GRID_SIZE + 3, 0, "                 AI %d wins!\n", state->winner + 1);
    sleep(1);
    refresh();

    sleep(5);
    clear();
    refresh();
}

void displayMenu(SharedState *state) {
    int choice;
    while (1) {
        printw("\n--- Battleship Menu ---\n");
        printw("1. Start New Game\n");
        printw("2. Load Saved Game\n"); 
        printw("3. Display Grids\n");
        printw("4. Re-locate Ships\n");
        printw("5. Exit\n");
        printw("Enter your choice: ");
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
                    initializeGame(state);
                    gameLoop(state);
                }
                break;
            case '3':
                displayJudgeView(&state->players[0], &state->players[1], 2);
                break;
            case '4':
                for (int i = 0; i < PLAYER_COUNT; i++) {
                    initializeGrid(state->players[i].grid, state->players[i].attacked);
                    automaticPlacement(state->players[i].grid);
                }
                break;
            case '5':
                exit(0);
            default:
                printw("Invalid choice. Please try again.\n");
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
