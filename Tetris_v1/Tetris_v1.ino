
#include <EEPROM.h>
#include "Tetramino.h"

// Game board
const byte BOARD_WIDTH = 5;   // The width of the play area
const byte BOARD_HEIGHT = 24; // The height of the play area
const byte BORDER_X = 3;  // Padding on the right side of the board
const byte BORDER_Y = 3;  // Padding on the bottom of the board
//const byte FIELD_WIDTH = 16;
const byte FIELD_HEIGHT = BOARD_HEIGHT + BORDER_Y;
const uint16_t BORDER_MASK = ~(~(0xffff << BOARD_WIDTH) << BORDER_X);
uint16_t field[FIELD_HEIGHT];

uint16_t collisionLine;

// The active tetramino
byte tetraminoType = TETRAMINO_NONE;
byte tetraminoR;  // Rotation 0-3
byte tetraminoX;  // x position in the field (zero is rightmost column, x increases to the left)
byte tetraminoY;  // y position in the field (zero is bottom row of board, y increases upwards)

// The game field is represented by an array of uint16_t, with one bit
// representing each block. The game board is the part of the field that
// fits on the screen. There is a minimum 3-bit-wide border around the
// game board on the sides and bottom. All of the field bits outside the
// board are ones, making them behave like solid walls and floor. This
// facilitates the game engine math.

// Similarly, each tetramino is represented by an array of four uint16_t.
// The 16 bits are interpreted as blocks in a 4 x 4 grid, and there is a
// separate uint16_t for each rotation of the same tetramino.
// See Tetramino.h for more details.

// @@ = field origin
// ## = tetramino origin
// '. = empty tetramino bit
// :; = empty board bit
//
// tetramino coordinates
//          3 2 1 0
//   []:;:;:;:;:;:;:;:;[][][] 10...
// 3 []:;:;'.'.[]'.:;:;[][][] 9
// 2 []:;:;'.'.[]'.:;:;[][][] 8
// 1 []:;:;'.[][]'.:;:;[][][] 7
// 0 []:;:;'.'.'.##:;:;[][][] 6
//   []:;:;:;:;:;:;:;:;[][][] 5
//   []:;:;:;:;:;:;:;:;[][][] 4
//   []:;:;:;:;:;;::;:;[][][] 3
//   [][][][][][][][][][][][] 2
//   [][][][][][][][][][][][] 1
//   [][][][][][][][][][][]@@ 0
//  ...10 9 8 7 6 5 4 3 2 1 0
//        field coordinates

// Game stats
uint16_t score = 0;
uint16_t linesCleared = 0;
uint16_t level = 1;
uint16_t fallPeriod = 1000;
//uint32_t nextFallMillis = 0;
uint32_t lastFallMillis = 0;
uint16_t highScore = 0;
char* highScoreInitials = "CTS "; // Four letters

// Music commands - top four bits = counter, bottom four bits = opcode
const byte
  COMMAND_SILENCE = 0x00,
  COMMAND_LEVEL_ONE = 0x01,
  COMMAND_LEVEL_UP = 0x02,
  COMMAND_GAME_OVER = 0x0d;

// EEPROM addresses
const byte
  EEPROM_RANDOM_SEED = 0, // uint32_t
  EEPROM_HIGH_SCORE = 4,  // uint16_t
  EEPROM_HIGH_SCORE_INITIALS = 6;  // 4 chars
  
void setup() {
  // DEBUG
  Serial.begin(115200);
  
  // Initialize the RNG, and change the seed value for next time
  uint32_t eepromRandomSeed;
  EEPROM.get(EEPROM_RANDOM_SEED, eepromRandomSeed);
  randomSeed(eepromRandomSeed);
  eepromRandomSeed = random();
  EEPROM.put(EEPROM_RANDOM_SEED, eepromRandomSeed);
  
  // Load the high score
  // TODO When we load the high score initials, if any are non-letters and non-space, reset high score
  EEPROM.get(EEPROM_HIGH_SCORE, highScore);
  // TODO Load highScoreInitials[]
  
  // Initialize functions
  initializeControl();
  initializeDisplay();
  initializeMusic();
}

void loop() {
  newGame();
  playGame();
  gameOver();
}

/******************************************************************************
 * Game State
 ******************************************************************************/

// Starts a new game.
void newGame() {
  score = 0;
  linesCleared = 0;
  level = getLevel(linesCleared);
  fallPeriod = getFallPeriod(level);
  
  clearBoard();
  drawBoard();
  
  sendMusicCommand(COMMAND_LEVEL_ONE);
}

// Plays a full game. This function does not return until game over, when a
// newly spawned tetramino collides with existing blocks.
void playGame() {
  while (true) {
    // Spawn a new piece
    setTetramino(random(TETRAMINO_COUNT));
    lastFallMillis = millis();
    
    drawBoard();
  
    // Check for collision (game over)
    if (isTetraminoCollision()) {
      return; // Game over
    }
    
    // Flag to stop dropping when a new piece is added
    bool canDropPiece = false;
    
    // Fall loop
    while (true) {
      
      // User input loop
      do {
        // Read buttons
        updateControl();
        bool draw = false;  // Only draw if something changed
        
        // Move left
        if (isLClick()) {
          tryMoveTetraminoLeft();
          draw = true;
        }
        
        // Move right
        if (isRClick()) {
          tryMoveTetraminoRight();
          draw = true;
        }
        
        // Rotate CCW
        long positionChange = getEncoderChange();
        if (positionChange != 0) {
          for (byte i = 0; i < 4; i++) {  // This should work as an infinite loop, but we use a for loop for safety
            if (!(positionChange > 0 ? tryRotateTetraminoCW() : tryRotateTetraminoCCW())) {
              // Couldn't rotate, check where the collision is and try to move away from the walls
              if (collisionLine & ~(0xffff << BORDER_X)) {  // Collision on right
                if (!tryMoveTetraminoLeft()) {
                  // Couldn't move left, break
                  break;
                }
              } else if (collisionLine & (0xffff << (BOARD_WIDTH + BORDER_X))) {  // Collision on left
                if (!tryMoveTetraminoRight()) {
                  // Couldn't move right, break
                  break;
                }
              } else {  // Collision in center
                break;
              }
            } else {
              break;  // Rotation succeeded
            }
          }
          draw = true;
        }

        // Drop
        /*if (isDClick()) {
          dropTetramino();
          draw = true;
          // TODO break drop loop so that piece cannot move after drop
        }*/
  
        if (draw) {
          drawBoard();
        }
        
        // Allow dropping if D is released
        canDropPiece |= !isDPress();
      } while (millis() < lastFallMillis + (canDropPiece && isDPress() ? 10 : fallPeriod));
      
      // Move the piece down
      if (canTetraminoMoveDown()) {
        tetraminoY--;
        lastFallMillis = millis();
        drawBoard();
      } else {
        // Piece landed
        break;
      }
    }
  
    // The active piece has landed, so assimilate it onto the board
    assimilateTetramino();
    
    // Count completed rows and drop remaining rows
    byte lineCount = 0;
    for (byte y = BORDER_Y; y < FIELD_HEIGHT; y++) {
      // Check whether this row is full
      if ((field[y] | BORDER_MASK) == 0xffff ) {
        // Row is full
        lineCount++;
      } else {
        // Row is not full
        if (lineCount != 0) {
          field[y - lineCount] = field[y];
          field[y] = BORDER_MASK;
        }
      }
    }
    
    if (lineCount != 0) {
      // Increase total count
      linesCleared += lineCount;
      
      // Calculate score
      score += getLineScore(lineCount);
      drawScore();
      
      // Level up
      uint16_t previousLevel = level;
      level = getLevel(linesCleared);
      fallPeriod = getFallPeriod(level);
      if (level > previousLevel) {
        sendMusicCommand(COMMAND_LEVEL_UP);
      }
    }
  }
}

// Draws the game over animation, displays the player's score, etc.
void gameOver() {
  // Clear the active tetramino
  tetraminoType = TETRAMINO_NONE;
  
  sendMusicCommand(COMMAND_GAME_OVER);
  
  // Fill animation
  const uint16_t CURTAIN_MILLIS = 1000 / BOARD_HEIGHT;
  for (byte y = 1; y <= BOARD_HEIGHT; y++) {
    drawBoard(true, y);
    delay(CURTAIN_MILLIS);
  }
//  delay(500);

  // TODO Allow score etc. animation to be interrupted by user input
  
  // Display score
  clearBoard();
  uint16_t scoreCopy = score;
  for (byte i = 0; i == 0 || (i < 4 && scoreCopy != 0); i++) {
//    setDisplayDigit3Wide(scoreCopy % 10, 1, BORDER_Y + 1 + 6 * i);
    setDisplayDigit5Wide(scoreCopy % 10, 0, BORDER_Y + 1 + 6 * i);
    scoreCopy /= 10;
  }
  
  // Raise curtain animation
  for (byte y = 1; y <= BOARD_HEIGHT; y++) {
    drawBoard(false, y - BOARD_HEIGHT);
    delay(CURTAIN_MILLIS);
  }
  delay(1000);
  
  
  /*byte y = BOARD_HEIGHT + BORDER_Y;
  field[y - 1] = 0x0000;
  uint16_t scoreDivider = 1;
  byte scoreDigits = 0;
  while (score / scoreDivider >= 10) {
    scoreDivider *= 10;
  }
  uint16_t scoreCopy = score;
  uint16_t scoreChange;
  while (scoreDivider != 0) {
    scoreChange = scoreCopy / scoreDivider;
    
    y -= 6;
    field[y - 1] = 0x0000;
    setDisplayDigit(scoreChange, 1, y);
    drawBoard();
    Serial.println(scoreChange);
    
    scoreCopy -= scoreChange * scoreDivider;
    scoreDivider /= 10;

    delay(500);
  }*/
  
  // TODO Display high score etc
  Serial.println("Game Over!");
  delay(1000);
}

// Clear the board (also fills the border).
void clearBoard() {
  for (byte y = 0; y < FIELD_HEIGHT; y++) {
    field[y] = y < BORDER_Y ? 0xffff : BORDER_MASK;
  }
}

/******************************************************************************
 * Game Stats
 ******************************************************************************/

// Gets the number of points earned for clearing the given number of lines.
uint16_t getLineScore(byte lines) {
  return lines * (lines + 1) / 2;
}

// Gets the current level. The level is implied by the number of lines cleared.
uint16_t getLevel(uint16_t linesCleared) {
  return linesCleared / 10 + 1;
}

// Gets the number of milliseconds between steps of a falling piece.
// The period is initially 1000 and decreases exponentially as the level increases. 
uint16_t getFallPeriod(uint16_t level) {
  return round(1000 * pow(0.774264, level - 1));  // 10x speed in level 10
}

/******************************************************************************
 * Teramino Movement
 ******************************************************************************/

// TODO Combine tryRotateTetraminoCW() with tryRotateTetraminoCCW().
// Rotates the active tetramino clockwise by 90 degrees if possible.
// Returns true if the tetramino was rotated successfully.
bool tryRotateTetraminoCW() {
  bool canRotateCW = canTetraminoRotateCW();
  if (canRotateCW) {
    tetraminoR = (tetraminoR + 3) % 4;
  }
  return canRotateCW;
}

// Rotates the active tetramino counterclockwise by 90 degrees if possible.
// Returns true if the tetramino was rotated successfully.
bool tryRotateTetraminoCCW() {
  bool canRotateCCW = canTetraminoRotateCCW();
  if (canRotateCCW) {
    tetraminoR = (tetraminoR + 1) % 4;
  }
  return canRotateCCW;
}

// Moves the active tetramino one step to the left if possible.
// Returns true if the tetramino was moved successfully.
bool tryMoveTetraminoLeft() {
  bool canMoveLeft = canTetraminoMoveLeft();
  if (canMoveLeft) {
    tetraminoX++;
  }
  return canMoveLeft;
}

// Moves the active tetramino one step to the right if possible.
// Returns true if the tetramino was moved successfully.
bool tryMoveTetraminoRight() {
  bool canMoveRight = canTetraminoMoveRight();
  if (canMoveRight) {
    tetraminoX--;
  }
  return canMoveRight;
}

// Drops the active tetramino instantaneously.
void dropTetramino() {
  while (tryMoveTetraminoDown());
}

// Moves the active tetramino one step down if possible.
// Returns true if the tetramino was moved successfully.
bool tryMoveTetraminoDown() {
  bool canMoveDown = canTetraminoMoveDown();
  if (canMoveDown) {
    tetraminoY--;
  }
  return canMoveDown;
}

/******************************************************************************
 * Active Teramino
 ******************************************************************************/

// Spawns a tetramino at the top of the screen
void setTetramino(byte type) {
  tetraminoType = type;
  tetraminoR = 0;
  tetraminoX = (BOARD_WIDTH - TETRAMINO_SIZE) / 2 + BORDER_X;
  tetraminoY = BOARD_HEIGHT - TETRAMINO_SIZE + BORDER_Y;
}

bool canTetraminoRotateCCW() {
  return !isTetraminoCollision(tetraminoType, (tetraminoR + 1) % 4, tetraminoX, tetraminoY);
}

bool canTetraminoRotateCW() {
  return !isTetraminoCollision(tetraminoType, (tetraminoR + 3) % 4, tetraminoX, tetraminoY);
}

bool canTetraminoMoveLeft() {
  return !isTetraminoCollision(tetraminoType, tetraminoR, tetraminoX + 1, tetraminoY);
}

bool canTetraminoMoveRight() {
  return !isTetraminoCollision(tetraminoType, tetraminoR, tetraminoX - 1, tetraminoY);
}

bool canTetraminoMoveDown() {
  return !isTetraminoCollision(tetraminoType, tetraminoR, tetraminoX, tetraminoY - 1);
}

// Returns true if any of the tetramino's bits overlap those already on the board
// TODO Redo logic (see diagram above)
bool isTetraminoCollision() {
  return isTetraminoCollision(tetraminoType, tetraminoR, tetraminoX, tetraminoY);
}

bool isTetraminoCollision(byte type, byte r, byte x, byte y) {
  const uint16_t tetraminoShape = TETRAMINO_SHAPES[type][r];
  for (byte i = 0; i < TETRAMINO_SIZE; i++) {
    uint16_t tetraminoLine = ((tetraminoShape >> (TETRAMINO_SIZE * i)) & TETRAMINO_MASK) << x;
    uint16_t fieldLine = field[y + i];
    collisionLine = tetraminoLine & fieldLine;
    
    if (collisionLine != 0) {
      return true;
    }
  }
  
  return false;
}

// Adds the active tetramino to the field.
void assimilateTetramino() {
  const uint16_t tetraminoShape = TETRAMINO_SHAPES[tetraminoType][tetraminoR];
  for (byte i = 0; i < TETRAMINO_SIZE; i++) {
    uint16_t tetraminoLine = ((tetraminoShape >> (TETRAMINO_SIZE * i)) & TETRAMINO_MASK) << tetraminoX;
    field[tetraminoY + i] |= tetraminoLine;
  }
}

/******************************************************************************
 * Text
 ******************************************************************************/

// 3-wide digits 0 to 9
const uint32_t BOARD_DIGITS[5] = {
  0b00110010100010110001110111111010,
  0b00001101100101001001001100010101,
  0b00011010010110110111110010010101,
  0b00101101001100100101001001110101,
  0b00010010111011111101110110010010
};

// 5-wide digits 0 to 5
const uint32_t BOARD_DIGITS_05[5] = {
  0b00011110000111110111111111101110,
  0b00100000000100001100000010010001,
  0b00011111111100110011100010010001,
  0b00000011000100001000011110010001,
  0b00111111000111111111100010001110
};

// 5-wide digits 6 to 9
const uint32_t BOARD_DIGITS_69[5] = {
  0b00000000000011110011100010001110,
  0b00000000000000001100010010010001,
  0b00000000000001111011100001011110,
  0b00000000000010001100010000110000,
  0b00000000000001110011101111101111
};

void setDisplayText(String str) {
  for (byte i = 0; i < str.length(); i++) {
    byte stringY = BOARD_HEIGHT - 6 * (i + 1) + BORDER_Y;
    char stringChar = str[i];

    if (isDigit(stringChar)) {
      setDisplayDigit3Wide(stringChar - '0', 1, stringY);
    } else {
      // TODO Implement letters
      for (byte r = 0; r < 5; r++) {
        field[stringY + r] = BORDER_MASK | (0x01 << r) << BORDER_X;
      }
    }
  }
  drawBoard();
}

void setDisplayDigit3Wide(byte digit, byte x, byte y) {
  for (byte r = 0; r < 5; r++) {
    field[y + r] = ((BOARD_DIGITS[r] >> (3 * digit)) & 0b111) << (BORDER_X + x);
  }
}

void setDisplayDigit5Wide(byte digit, byte x, byte y) {
  for (byte r = 0; r < 5; r++) {
    field[y + r] = (((digit <= 5 ? BOARD_DIGITS_05[r] : BOARD_DIGITS_69[r]) >> (5 * (digit % 6))) & 0b11111) << (BORDER_X + x);
  }
}

