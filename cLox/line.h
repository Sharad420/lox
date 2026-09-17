#ifndef clox_line_h
#define clox_line_h

// The basic unit for a run-length encoding representation.
typedef struct {
    int line;
    int run;
} LineRun;

// The line run array.
typedef struct {
    int count;
    int capacity;
    LineRun* lines;
} LineRunArray;

void initLineRunArray(LineRunArray* array);
void writeLineRunArray(LineRunArray* array, int line);
void freeLineRunArray(LineRunArray* array);
int getLine(const LineRunArray* array, int offset);

#endif