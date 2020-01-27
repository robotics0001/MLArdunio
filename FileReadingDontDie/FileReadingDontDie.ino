#include <SD.h>
#include <KDE.h>

//Amount of millisectonds to train for


#define DATACOLLECT 0
#define LEARN 1
#define OPERATE 2
#define RELEARN 3
#define H 5.5
#define MAX_FLOAT_VAL 3.4028235e38
#define MIN_FLOAT_VAL -MAX_FLOAT_VAL

#define H 0.5

#define AMP_PIN A3

const float TRAINING_PERIOD_MINS = 0.5;
const float fPEM_MINS = 1 / 6.0;
const float TRAINING_PERIOD = TRAINING_PERIOD_MINS * 1000 * 60;
const float fPEM = fPEM_MINS * 60 * 1000;

const unsigned int MAX_NUMBER_OF_FILES = floor(TRAINING_PERIOD_MINS / fPEM_MINS);

boolean allowWrite = true;
unsigned int state;
int lastFile = 0;
float lastTime = 0;
File file;
File bam;

long startTime;

long *timeData;
float *ampData;

Kernel *kernels;
float *mins;

unsigned int dataSize;
unsigned int minsSize;

String data;
unsigned int lastTimes;

void setup() {
  Serial.begin(9600);
  if (!SD.begin(4)) {
    Serial.println("begin error");
    return;
  }

  pinMode(AMP_PIN, INPUT);
  pinMode(8, OUTPUT);
  digitalWrite(8, HIGH);
  /*for (unsigned int i = 0; i < MAX_NUMBER_OF_FILES; i++) {
    SD.remove("MS_" + String(i) + ".txt");
  }*/
  data = "";
  lastTimes = 0;
  startTime = millis();
  state = LEARN;
}
void loop() {

  //Datacollect state is WIP
  if (state == DATACOLLECT) {
    long timeMs = millis();
    float amps = getAmps();

    unsigned int times = fmod((timeMs - startTime) / round(fPEM), MAX_NUMBER_OF_FILES);
    Serial.print("delta T: ");
    Serial.print(timeMs - startTime);
    Serial.print(", ");
    Serial.print("lim: ");
    Serial.println(TRAINING_PERIOD);

    if (timeMs - startTime < TRAINING_PERIOD) {
      data = String(timeMs - startTime) + "," + String(amps) + '\r' + '\n';

      //Serial.println(data);
      if (times != lastTimes) {
        data = String(timeMs - startTime) + "," + String(amps);
        appendToFile("MS_" + String(times - 1) + ".txt", data);
        Serial.println(data);
        data = "";
        Serial.println("RESET");
        lastTimes = times;
        Serial.print("file: ");
        Serial.println(times - 1);
      }
      else {
        appendToFile("MS_" + String(times) + ".txt", data);
      }
    }
    else {
      data = String(timeMs - startTime) + "," + String(amps);
      appendToFile("MS_" + String(MAX_NUMBER_OF_FILES - 1) + ".txt", data);
      data = "";
      Serial.print("file: ");
      Serial.println(MAX_NUMBER_OF_FILES - 1);
      state = LEARN;
    }
  }
  else if (state == OPERATE) {
    float val = Kernel::kernelConsensus(kernels, dataSize, getAmps());
    if (val > mins[0]) {
      Serial.println("On");
    }
    else {
      Serial.println("Off");
    }
  }
  else if (state == LEARN) {
    dataSize = getNumDataPoints();

    ampData = (float*) malloc(dataSize * sizeof(float));
    timeData = (long*) malloc(dataSize * sizeof(long));

    getAllData(timeData, ampData, dataSize);

    float maxAmps = 0;
    for (unsigned int i = 0;  i < dataSize; i++) {
      float amp;
      amp = ampData[i];
      if(amp > maxAmps) {
        maxAmps = amp;
      }
      Serial.print("max: ");
      Serial.print(maxAmps);
      Serial.print(" amps: ");
      Serial.println(amp);
    }
    Serial.println(dataSize);
    delay(1000);

    kernels = (Kernel*) malloc(dataSize * sizeof(Kernel));

    for (unsigned int i = 0; i < dataSize; i++) {
      Serial.println(i);
      float ampDataPt;
      ampDataPt = ampData[i];
      Kernel k = Kernel(ampDataPt, H);
      kernels[i] = k;
    }



    findMin(kernels, dataSize, 0.0, maxAmps, 1, 0.5);


    delay(1000);

    for (unsigned int i = 0;  i < minsSize; i++) {
      Serial.print("divider: ");
      Serial.println(mins[i]);
    }

    state = 255;
  }
}

void getAllData(long * timeOutput, float * ampOutput, unsigned int listSize) {
  unsigned int tracker = 0;
  for (unsigned int i = 0; i < MAX_NUMBER_OF_FILES; i++) {

    String num = String(i);

    file = SD.open("MS_" + num + ".txt", FILE_READ);

    if (!file) {
      Serial.println("open error opening file MS_" + num + ".txt");
      delay(1000);
      return;
    }
    long x;
    float y;

    while (readVals(&x, &y)) {
      timeOutput[tracker] = x;
      ampOutput[tracker] = y;
      tracker++;
    }
    file.close();

  }
}
unsigned int getNumDataPoints() {
  unsigned int numDataPoints = 0;

  for (int i = 0; i < MAX_NUMBER_OF_FILES; i++) {
    File dataFile = SD.open("MS_" + String(i) + ".txt", FILE_READ);
    if (dataFile) {
      while (dataFile.available()) {
        if (dataFile.read() == ',') {
          numDataPoints++;
        }
      }
      dataFile.close();
    }
    else {
      Serial.println("error opening MS_" + (String) i + ".txt");
    }
  }
  return numDataPoints;
}

bool readLine(File & f, char* line, size_t maxLen) {
  for (size_t n = 0; n < maxLen; n++) {
    int c = f.read();
    if ( c < 0 && n == 0) {
      return false;  // EOF
    }
    if (c < 0 || c == '\n') {
      line[n] = 0;
      return true;
    }
    line[n] = c;
  }
  return false; // line too long
}

bool readVals(long * v1, float * v2) {
  char line[40], *ptr, *str;
  if (!readLine(file, line, sizeof(line))) {
    return false;  // EOF or too long
  }

  *v1 = strtol(line, &ptr, 10);

  if (ptr == line) {
    Serial.println("bad number");
    return false;  // bad number if equal
  }

  while (*ptr) {
    if (*ptr++ == ',') break;
  }
  *v2 = (float) strtod(ptr, &str);
  return str != ptr;  // true if number found
}

void findMin(Kernel * kernels, unsigned int dataSize, float lowerBound, float upperBound, float algStep, float lossThreshold) {
  float currentX = lowerBound;
  float lastValue = Kernel::kernelConsensus(kernels, dataSize, currentX);
  currentX += algStep / 2;
  float currentValue = Kernel::kernelConsensus(kernels, dataSize, currentX);
  float delta = currentValue - lastValue;

  while (delta == 0.0) {
    currentX += algStep;
    lastValue = currentValue;
    currentValue = Kernel::kernelConsensus(kernels, dataSize, currentX);
    delta = currentValue - lastValue;
  }

  currentX += algStep;
  lastValue = currentValue;
  currentValue = Kernel::kernelConsensus(kernels, dataSize, currentX);
  delta = currentValue - lastValue;

  int lastSgn = signum(delta);
  int sgn = lastSgn;

  unsigned int arrSize = 0;

  float currentXCpy = currentX;
  float lastSgnCpy = lastSgn;
  float sgnCpy = sgn;
  float lastValueCpy = lastValue;
  float currentValueCpy = currentValue;

  //Find how many mins there are
  while (currentXCpy < upperBound) {
    lastSgnCpy = sgnCpy;
    currentXCpy += algStep;
    lastValueCpy = currentValueCpy;

    currentValueCpy = Kernel::kernelConsensus(kernels, dataSize, currentXCpy);
    delta = currentValueCpy - lastValueCpy;
    sgnCpy = signum(delta);

    if (sgnCpy - lastSgnCpy > 0) {
      arrSize++;
    }
  }

  float maxRanges[arrSize];
  unsigned int index = 0;

  //Find the upper bound of all the mins
  while (currentX < upperBound) {
    lastSgn = sgn;
    currentX += algStep;
    lastValue = currentValue;

    currentValue = Kernel::kernelConsensus(kernels, dataSize, currentX);
    delta = currentValue - lastValue;
    sgn = signum(delta);

    if (sgn - lastSgn > 0) {
      maxRanges[index] = currentX;
      index++;
    }
  }

  boolean lastMidFloored = false;
  float dividers[arrSize];
  float flooredDividers[arrSize];
  unsigned int usedVals = 0;
  unsigned int usedFloorVals = 0;

  //Refine the estimated mins
  for (unsigned int i = 0; i < arrSize; i++) {
    lastValue = Kernel::kernelConsensus(kernels, dataSize, maxRanges[i] - algStep);
    float mid = (2 * maxRanges[i] - algStep) / 2;
    currentValue = Kernel::kernelConsensus(kernels, dataSize, mid);
    float lBound = maxRanges[i] - algStep;
    float uBound = maxRanges[i];
    while (abs(currentValue - lastValue) > lossThreshold) {
      float rmid = (mid + uBound) / 2;
      float lmid = (lBound + mid) / 2;

      float rmidVal = Kernel::kernelConsensus(kernels, dataSize, rmid);
      float lmidVal = Kernel::kernelConsensus(kernels, dataSize, lmid);

      mid = rmidVal > lmidVal ? lmid : rmid;

      lastValue = currentValue;
      currentValue = Kernel::kernelConsensus(kernels, dataSize, mid);
    }

    if (abs(currentValue - lastValue) == 0) {
      flooredDividers[usedFloorVals] = mid;
      usedFloorVals++;
      lastMidFloored = true;
    }
    else if (lastMidFloored) {
      dividers[usedVals] = (flooredDividers[usedFloorVals - 1] + mid) / 2;
      usedVals++;
      lastMidFloored = false;
    }
    else {
      dividers[usedVals] = mid;
      usedVals++;
    }
  }

  mins = (float*) malloc(usedVals * sizeof(float));
  minsSize = usedVals;

  for (unsigned int i = 0; i < usedVals; i++) {
    mins[i] = dividers[i];
  }
}

float getAmps() {
  float amps;
  const float varVolt = .00583;
  const float varProccess = 5e-6;
  float Pc = 0;
  float G = 0;
  float P = 1;
  float Xp = 0;
  float Zp = 0;
  float Xe = 0;
  for (int i = 0; i < 50; i++) {
    float vOut = (analogRead(AMP_PIN) / 1023.0) * 5000;
    float current = (vOut);
    float shift = 2500 - current;
    amps = abs(shift);

    Pc = P + varProccess;
    G = Pc / (Pc + varVolt);
    P = (1 - G) * Pc;
    Xp = Xe;
    Zp = Xp;
    Xe = G * (amps - Zp) + Xp;
  }

  return Xe;
}

bool appendToFile(String filePath, String data) {
  File writeFile;
  File readFile;

  if (!SD.exists(filePath)) {
    File temp;
    temp = SD.open(filePath, FILE_WRITE);
    temp.close();
  }

  readFile = SD.open(filePath, FILE_READ);
  writeFile = SD.open("temp.txt", FILE_WRITE);
  if (readFile && writeFile) {
    readFile.seek(0);
    writeFile.seek(0);
    while (readFile.available()) {
      char c;
      c = readFile.read();
      writeFile.write(c);
    }
    writeFile.print(data);
    readFile.close();
    writeFile.close();

    SD.remove(filePath);
    File copied;
    File readIn;
    copied = SD.open(filePath, FILE_WRITE);
    readIn = SD.open("temp.txt", FILE_READ);

    if (readIn && copied) {
      readIn.seek(0);
      copied.seek(0);
      while (readIn.available()) {
        char c2;
        c2 = readIn.read();
        copied.write(c2);
      }
      readIn.close();
      copied.close();

      SD.remove("temp.txt");
      return true;
    }
    else {
      readIn.close();
      copied.close();
      return false;
    }
  }
  else {
    readFile.close();
    writeFile.close();
    return false;
  }
}

int signum(float val) {
  return (0.0 < val) - (val < 0.0);
}
