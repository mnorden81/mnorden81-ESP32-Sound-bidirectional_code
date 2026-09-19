// XTronical I2S Audio Library, angepasst: Ringpuffer für I2S (ESP32)
// Basis: XT_I2S_Audio - Kopie
// Änderungen: Ringbuffer + Consumer Task + Stabilitäts- und Performance-Fixes
// (c) XTronical 2018-2021, Anpassungen 2025

#include "XT_I2S_Audio.h"
#include <math.h>
#include <cstring>
#include <algorithm>
#include <hardwareSerial.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "SD.h"

WavHeader_Struct WavHeader; // Used as a place to store the header data from the wav data

// Einheitlicher Schalter fuer die sonst unbedingten, haeufigen
// Debug-Ausgaben (Play()/Init()/WAV-Header-Dump). Standardmaessig aus;
// ueber ein #define hier oder spaeter einen Web-/CRSF-Schalter aktivierbar.
bool g_audioDebug = false;

static portMUX_TYPE s_ringInitMux = portMUX_INITIALIZER_UNLOCKED;

/**********************************************************************************************************************/
/* SD-Zugriff + Hintergrund-Nachlade-Task (Punkt 2)                                                                    */
/**********************************************************************************************************************/
// Serialisiert allen SD-Zugriff (SD.open()/read()/close()/seek()) ueber alle
// XT_Wav_Class-Objekte hinweg: sowohl der Hintergrund-Task als auch die
// synchronen Notfall-Rueckfaelle (ReadFile()) und LoadWavFile()/UnLoadWavFile()
// im Hauptloop nehmen denselben Mutex, bevor sie die SD-Karte anfassen.
static SemaphoreHandle_t sdMutex = nullptr;
QueueHandle_t g_sdRefillQueue = nullptr;
static TaskHandle_t sdReaderTaskHandle = nullptr;
static volatile uint32_t s_sdUnderrunCount = 0;

// Siehe Erklaerung in XT_I2S_Audio.h - erlaubt WebServerManager.cpp,
// denselben Mutex wie der Audio-Player zu nehmen, bevor es fuer das
// Konfigurations-Backup selbst auf die SD-Karte zugreift.
bool sdMutexTake(uint32_t timeoutMs) {
    if (!sdMutex) return true; // noch nicht angelegt -> noch kein Sound geladen, kein Konflikt moeglich
    return xSemaphoreTake(sdMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}
void sdMutexGive() {
    if (sdMutex) xSemaphoreGive(sdMutex);
}

static void sdReaderTaskFn(void *)
{
    XT_Wav_Class *obj;
    for (;;)
    {
        if (xQueueReceive(g_sdRefillQueue, &obj, portMAX_DELAY) == pdTRUE && obj != nullptr)
        {
            xSemaphoreTake(sdMutex, portMAX_DELAY);
            obj->ReadIntoBack();
            xSemaphoreGive(sdMutex);
        }
    }
}

/**********************************************************************************************************************/
/* Playlist-Mutex (Punkt 1)                                                                                            */
/**********************************************************************************************************************/
// Schuetzt FirstPlayListItem/LastPlayListItem sowie die Previous/NextItem-
// Verkettung. Play()/Stop()/StopAllSounds() werden aus dem Arduino-loop()
// aufgerufen, MixSamples() dagegen aus dem I2SProducer-Task (siehe unten) -
// ohne dieses Schloss konnten beide Seiten gleichzeitig an denselben
// Zeigern schreiben (beschaedigte Liste, verlorene/haengende Sounds).
// Rekursiv, weil Play()/Stop() intern AlreadyPlaying()/RemoveFromPlayList()/
// StopAllSounds() aufrufen, die denselben Schutz brauchen - mit einem
// normalen Mutex wuerde das im selben Task sofort blockieren (Deadlock).
static SemaphoreHandle_t playlistMutex = nullptr;
struct PlaylistLockGuard {
    PlaylistLockGuard()  { xSemaphoreTakeRecursive(playlistMutex, portMAX_DELAY); }
    ~PlaylistLockGuard() { xSemaphoreGiveRecursive(playlistMutex); }
};


/**********************************************************************************************************************/
/* Ringbuffer configuration                                                                                           */
/**********************************************************************************************************************/
// SLOT_BYTES muss Vielfaches von 4 sein. Wähle passend zur i2s_config.dma_buf_len (z.B. 256 oder 512).
#define SLOT_BYTES       512
#define SLOT_WORDS       (SLOT_BYTES / 4)
#define RING_SLOTS       16 // erhöht auf 16 für besseren Puffer
static_assert((RING_SLOTS & (RING_SLOTS - 1)) == 0, "RING_SLOTS must be power of two");

static uint32_t ringBuffer[RING_SLOTS][SLOT_WORDS];
// use larger index type to avoid surprises if RING_SLOTS grows
static volatile uint32_t ringHead = 0;
static volatile uint32_t ringTail = 0;
static SemaphoreHandle_t ringMutex = nullptr;
static SemaphoreHandle_t slotCountSem = nullptr;  // gefüllte Slots
static SemaphoreHandle_t freeSlotSem = nullptr;   // freie Slots
static TaskHandle_t i2sConsumerTaskHandle = nullptr;
static TaskHandle_t i2sProducerTaskHandle = nullptr;
static volatile XT_I2S_Class *s_pI2SAudioInstance = nullptr;  // Pointer zur Instanz für Producer Task

/**********************************************************************************************************************/
/* global functions                                                                                                   */
/**********************************************************************************************************************/

// Fixed-point volume: Volume percent 0..100 mapped to Q8.8 (scale = (Volume*256)/100)
void SetVolume(int16_t *Left, int16_t *Right, int16_t Volume)
{
    if (Volume < 0) return;
    if (Volume == 100) return; // no-op fast path
    uint32_t scale = (uint32_t(Volume) * 256u) / 100u; // Q8.8
    int32_t l = (int32_t)(*Left) * (int32_t)scale;
    int32_t r = (int32_t)(*Right) * (int32_t)scale;
    *Left = int16_t((l + 128) >> 8);
    *Right = int16_t((r + 128) >> 8);
}

/**********************************************************************************************************************/
/* XT_PlayListItem_Class                                                                                              */
/**********************************************************************************************************************/

XT_PlayListItem_Class::XT_PlayListItem_Class()
{
    RepeatForever = false;
    Repeat = 0;
    RepeatIdx = 0;
}

void XT_PlayListItem_Class::NextSample(int16_t *Left, int16_t *Right)
{
}
void XT_PlayListItem_Class::Init()
{
}

/**********************************************************************************************************************/
/* Wave class                                                                                                         */
/**********************************************************************************************************************/
void XT_Wave_Class::NextSample(int16_t *Left, int16_t *Right)
{
}
void XT_Wave_Class::Init(int8_t Note)
{
}

/**********************************************************************************************************************/
/* XT_WAV_Class                                                                                                       */
/**********************************************************************************************************************/

#define DATA_CHUNK_ID 0x61746164
#define FMT_CHUNK_ID 0x20746d66
#define longword(bfr, ofs) (bfr[ofs + 3] << 24 | bfr[ofs + 2] << 16 | bfr[ofs + 1] << 8 | bfr[ofs + 0])

XT_Wav_Class::XT_Wav_Class(const String &name) // Load data from file on SD Card
    : FileName{name}
{
    SamplesBufferIsEmpty = true;
    // Samples/BackSamples werden bewusst NICHT hier angefordert (siehe
    // Kommentar in XT_I2S_Audio.h) - dieser Konstruktor laeuft fuer ALLE
    // Sound-Objekte VOR setup(), also auch vor WiFi.softAP().
    // AllocateBuffers() wird stattdessen explizit aus setup() aufgerufen,
    // NACH dem WLAN-AP-Start.
}

// Siehe ausfuehrliche Begruendung in XT_I2S_Audio.h beim Feld "Samples".
// Holt die beiden 1024-Byte-Puffer vom Heap statt sie als feste Arrays im
// Objekt zu tragen. Gefahrlos mehrfach aufrufbar. malloc() statt new: gibt
// im Fehlerfall nullptr statt eine Exception zu werfen (Arduino-ESP32 baut
// i.d.R. ohne C++-Exceptions).
void XT_Wav_Class::AllocateBuffers()
{
    if (Samples == nullptr)
    {
        Samples = (uint8_t *)malloc(NUM_BYTES_TO_READ_FROM_FILE);
        if (Samples == nullptr)
            Serial.printf("!!! FEHLER: Samples-Puffer (%s) - kein Speicher mehr !!!\n", FileName.c_str());
    }
    if (BackSamples == nullptr)
    {
        BackSamples = (uint8_t *)malloc(NUM_BYTES_TO_READ_FROM_FILE);
        if (BackSamples == nullptr)
            Serial.printf("!!! FEHLER: BackSamples-Puffer (%s) - kein Speicher mehr !!!\n", FileName.c_str());
    }
}

/****** WAV helper methods (improved checks) ****************************************/

bool XT_Wav_Class::ValidWavData(WavHeader_Struct *Wav)
{
    if (memcmp(Wav->RIFFSectionID, "RIFF", 4) != 0)
    {
        Serial.print("Invalid data - Not RIFF format");
        return false;
    }
    if (memcmp(Wav->RiffFormat, "WAVE", 4) != 0)
    {
        Serial.print("Invalid data - Not Wave file");
        return false;
    }
    if (memcmp(Wav->FormatSectionID, "fmt", 3) != 0)
    {
        Serial.print("Invalid data - No format section found");
        return false;
    }
    if (memcmp(Wav->DataSectionID, "data", 4) != 0)
    {
        Serial.print("Invalid data - data section not found");
        return false;
    }
    if (Wav->FormatID != 1)
    {
        Serial.print("Invalid data - format Id must be 1");
        return false;
    }
    if (Wav->FormatSize < 16)
    {
        Serial.print("Invalid data - format section size must be at least 16.");
        return false;
    }
    if ((Wav->NumChannels != 1) && (Wav->NumChannels != 2))
    {
        Serial.print("Invalid data - only mono or stereo permitted.");
        return false;
    }
    if (Wav->SampleRate > 48000)
    {
        Serial.print("Invalid data - Sample rate cannot be greater than 48000");
        return false;
    }
    if ((Wav->BitsPerSample != 8) && (Wav->BitsPerSample != 16))
    {
        Serial.print("Invalid data - Only 8 or 16 bits per sample permitted.");
        return false;
    }
    // additional consistency checks
    uint32_t expectedByteRate = Wav->SampleRate * Wav->NumChannels * (Wav->BitsPerSample / 8);
    if (Wav->ByteRate != expectedByteRate)
    {
        Serial.print("Warning: ByteRate inconsistent with SampleRate/Channels/BitsPerSample");
    }
    return true;
}

void XT_Wav_Class::PrintData(const char *Data, uint8_t NumBytes)
{
    for (uint8_t i = 0; i < NumBytes; i++)
        Serial.print(Data[i]);
    Serial.println();
}

void XT_Wav_Class::DumpWAVHeader(WavHeader_Struct *Wav)
{
    if (memcmp(Wav->RIFFSectionID, "RIFF", 4) != 0)
    {
        Serial.print("Not a RIFF format file - ");
        PrintData(Wav->RIFFSectionID, 4);
        return;
    }
    if (memcmp(Wav->RiffFormat, "WAVE", 4) != 0)
    {
        Serial.print("Not a WAVE file - ");
        PrintData(Wav->RiffFormat, 4);
        return;
    }
    if (memcmp(Wav->FormatSectionID, "fmt", 3) != 0)
    {
        Serial.print("fmt ID not present - ");
        PrintData(Wav->FormatSectionID, 3);
        return;
    }
    if (memcmp(Wav->DataSectionID, "data", 4) != 0)
    {
        Serial.print("data ID not present - ");
        PrintData(Wav->DataSectionID, 4);
        return;
    }
    Serial.println();
    Serial.print("Total size :");
    Serial.println(Wav->Size);
    Serial.print("Format section size :");
    Serial.println(Wav->FormatSize);
    Serial.print("Wave format :");
    Serial.println(Wav->FormatID);
    Serial.print("Channels :");
    Serial.println(Wav->NumChannels);
    Serial.print("Sample Rate :");
    Serial.println(Wav->SampleRate);
    Serial.print("Byte Rate :");
    Serial.println(Wav->ByteRate);
    Serial.print("Block Align :");
    Serial.println(Wav->BlockAlign);
    Serial.print("Bits Per Sample :");
    Serial.println(Wav->BitsPerSample);
    Serial.print("Data Size :");
    Serial.println(Wav->DataSize);
}

bool XT_Wav_Class::OpenWavFile()
{
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
    // v7.09 Bugfix: falls WavFile von einem frueheren LoadWavFile() noch
    // offen ist (z.B. wenn LoadWavFile() zweimal hintereinander aufgerufen
    // wird, ohne dass dazwischen UnLoadWavFile() lief), hier defensiv erst
    // schliessen - sonst haette man zwei offene Handles auf dieselbe Datei
    // gleichzeitig und verbraucht dabei unnoetig einen weiteren der knappen
    // File-Deskriptoren (siehe SDCardInit()/max_files in ESP32-RC-Sound.ino).
    if (WavFile) WavFile.close();
    WavFile = SD.open(FileName); // Open the wav file
    if (WavFile == false)
    {
        Serial.print("Could not open :");
        Serial.println(FileName);
        FileOK = false;
        if (sdMutex) xSemaphoreGive(sdMutex);
        return false;
    }

    // RIFF-Chunks werden iterativ durchsucht statt starr 44 Byte zu lesen,
    // damit auch WAV-Dateien mit JUNK/LIST/bext-Chunks vor dem fmt-/data-
    // Chunk oder mit erweitertem fmt-Chunk (WAVE_FORMAT_EXTENSIBLE,
    // FormatSize 18/40 statt 16) korrekt eingelesen werden.
    uint8_t riff[12];
    if (WavFile.read(riff, 12) != 12)
    {
        Serial.println("Invalid data - RIFF-Header zu kurz");
        WavFile.close(); FileOK = false; if (sdMutex) xSemaphoreGive(sdMutex); return false;
    }
    memcpy(WavHeader.RIFFSectionID, riff, 4);
    WavHeader.Size = longword(riff, 4);
    memcpy(WavHeader.RiffFormat, riff + 8, 4);
    if (memcmp(WavHeader.RIFFSectionID, "RIFF", 4) != 0)
    {
        Serial.println("Invalid data - Not RIFF format");
        WavFile.close(); FileOK = false; if (sdMutex) xSemaphoreGive(sdMutex); return false;
    }
    if (memcmp(WavHeader.RiffFormat, "WAVE", 4) != 0)
    {
        Serial.println("Invalid data - Not Wave file");
        WavFile.close(); FileOK = false; if (sdMutex) xSemaphoreGive(sdMutex); return false;
    }

    bool haveFmt = false, haveData = false;
    uint32_t dataChunkSize = 0, dataChunkOffset = 0;

    // Bis zu 64 Chunks absuchen (Sicherheitsgrenze gegen defekte/endlose Dateien)
    for (int guard = 0; guard < 64 && !(haveFmt && haveData); ++guard)
    {
        uint8_t chdr[8];
        if (WavFile.read(chdr, 8) != 8) break; // Dateiende erreicht
        uint32_t chunkSize = longword(chdr, 4);

        if (memcmp(chdr, "fmt ", 4) == 0)
        {
            uint8_t fmtBuf[40];
            uint32_t toRead = chunkSize < sizeof(fmtBuf) ? chunkSize : sizeof(fmtBuf);
            if (toRead < 16 || WavFile.read(fmtBuf, toRead) != (int)toRead) break;
            memcpy(WavHeader.FormatSectionID, "fmt", 3);
            WavHeader.FormatSize    = chunkSize; // tatsaechliche Groesse (kann >16 sein, z.B. Extensible)
            WavHeader.FormatID      = fmtBuf[0] | (fmtBuf[1] << 8);
            WavHeader.NumChannels   = fmtBuf[2] | (fmtBuf[3] << 8);
            WavHeader.SampleRate    = longword(fmtBuf, 4);
            WavHeader.ByteRate      = longword(fmtBuf, 8);
            WavHeader.BlockAlign    = fmtBuf[12] | (fmtBuf[13] << 8);
            WavHeader.BitsPerSample = fmtBuf[14] | (fmtBuf[15] << 8);
            if (chunkSize > toRead) WavFile.seek(WavFile.position() + (chunkSize - toRead));
            if (chunkSize & 1) WavFile.seek(WavFile.position() + 1); // Chunks sind wortweise (gerade) ausgerichtet
            haveFmt = true;
        }
        else if (memcmp(chdr, "data", 4) == 0)
        {
            memcpy(WavHeader.DataSectionID, "data", 4);
            dataChunkSize   = chunkSize;
            dataChunkOffset = WavFile.position();
            haveData = true;
            if (!haveFmt) WavFile.seek(WavFile.position() + chunkSize + (chunkSize & 1)); // weitersuchen nach fmt
        }
        else
        {
            // Unbekannter Chunk (z.B. JUNK, LIST, bext, ...) - ueberspringen
            WavFile.seek(WavFile.position() + chunkSize + (chunkSize & 1));
        }
    }

    if (!haveFmt || !haveData)
    {
        Serial.println("Invalid data - fmt/data Chunk nicht gefunden");
        WavFile.close(); FileOK = false; if (sdMutex) xSemaphoreGive(sdMutex); return false;
    }

    WavHeader.DataSize = dataChunkSize;
    DataStart = dataChunkOffset;
    WavFile.seek(DataStart);

    FileOK = true;
    if (sdMutex) xSemaphoreGive(sdMutex);
    return true;
}

void XT_Wav_Class::UnLoadWavFile()
{
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
    if (WavFile) WavFile.close();
    if (sdMutex) xSemaphoreGive(sdMutex);
}

void XT_Wav_Class::LoadWavFile()
{
    if (OpenWavFile())
    {
        if (ValidWavData(&WavHeader))
        {
            if (g_audioDebug) { DumpWAVHeader(&WavHeader); Serial.println(); }
        }
        else
        {
            Serial.print("Ivalid Wave file header! Filename: ");
            Serial.println(FileName);
            // Mark file as invalid so playback won't attempt to use it
            FileOK = false;
            WavFile.close();
            return;
        }

        SampleRate = WavHeader.SampleRate;
        BytesPerSample = (WavHeader.BitsPerSample / 8) * WavHeader.NumChannels;
        NumChannels = WavHeader.NumChannels;
        DataSize = WavHeader.DataSize;
        IncreaseBy = float(SampleRate) / SAMPLES_PER_SEC;
        PlayingTime = (1000u * DataSize) / (uint32_t)(SampleRate * BytesPerSample);
        WavFile.seek(DataStart); // Start der eigentlichen PCM-Daten (Punkt 14: nicht mehr fix 44)
        TotalBytesRead = 0; // Clear to no bytes read in so far
        SamplesBufferIsEmpty = true;
        BackReady = false;
        BackRequested = false;

        Speed = 1.0;
        Volume = 100;
    }
}

void XT_Wav_Class::ReadFile()
{
    // Synchroner Lesepfad - wird nur noch als Notfall-Rueckfall in NextSample()
    // benutzt (Hintergrund-Task kam nicht rechtzeitig hinterher), sowie fuer
    // den allerersten Fuellvorgang in Init(). sdMutex wird vom Aufrufer gehalten.
    // Samples ist ein Heap-Pointer (siehe AllocateBuffers()) - Notbremse,
    // falls doch mal vor der Zuteilung oder nach einem fehlgeschlagenen
    // malloc() hierher verzweigt wird.
    if (Samples == nullptr)
    {
        LastNumBytesRead = 0;
        SamplesBufferIsEmpty = true;
        return;
    }
    if (TotalBytesRead >= DataSize)
    {
        LastNumBytesRead = 0;
        SamplesBufferIsEmpty = true;
        return;
    }

    if (TotalBytesRead + NUM_BYTES_TO_READ_FROM_FILE > DataSize)
        LastNumBytesRead = DataSize - TotalBytesRead;
    else
        LastNumBytesRead = NUM_BYTES_TO_READ_FROM_FILE;

    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
    int bytes = WavFile.read(Samples, LastNumBytesRead);
    if (sdMutex) xSemaphoreGive(sdMutex);
    if (bytes <= 0)
    {
        Serial.print("ReadFile: read failed or EOF, bytes=");
        Serial.println(bytes);
        LastNumBytesRead = 0;
        SamplesBufferIsEmpty = true;
    }
    else
    {
        LastNumBytesRead = bytes;
        SamplesDataIdx = 0;
        TotalBytesRead += LastNumBytesRead;
        SamplesBufferIsEmpty = false;
    }
}

// Wird AUSSCHLIESSLICH vom SD-Lese-Task aufgerufen (sdMutex wird dort gehalten).
// Liest den naechsten Block in BackSamples, OHNE den aktuell spielenden
// Front-Puffer (Samples) anzufassen - dadurch kann NextSample() im I2S-Task
// weiterlaufen, waehrend im Hintergrund die naechste Portion von der SD-Karte
// nachgeladen wird (Punkt 2).
void XT_Wav_Class::ReadIntoBack()
{
    // Siehe Kommentar in ReadFile().
    if (BackSamples == nullptr)
    {
        BackNumBytesRead = 0;
        BackReady = true;
        return;
    }
    if (!FileOK || TotalBytesRead >= DataSize)
    {
        BackNumBytesRead = 0;
        BackReady = true;
        return;
    }

    uint32_t toRead = (TotalBytesRead + NUM_BYTES_TO_READ_FROM_FILE > DataSize)
        ? (DataSize - TotalBytesRead) : NUM_BYTES_TO_READ_FROM_FILE;

    int bytes = WavFile.read(BackSamples, toRead);
    if (bytes <= 0)
    {
        BackNumBytesRead = 0;
    }
    else
    {
        BackNumBytesRead = (uint16_t)bytes;
        TotalBytesRead += BackNumBytesRead; // Lese-Cursor sofort weiterschieben (Vorausschau)
    }
    BackReady = true;
}

void XT_Wav_Class::RequestBackFill()
{
    if (!FileOK || BackRequested || BackReady) return;
    if (TotalBytesRead >= DataSize) return;
    BackRequested = true;
    XT_Wav_Class *self = this;
    if (g_sdRefillQueue) xQueueSend(g_sdRefillQueue, &self, 0); // nicht blockierend
}

void XT_Wav_Class::Init()
{
    if (g_audioDebug) { Serial.print("XT_Wav_Class::Init() called for: "); Serial.println(FileName); }
    LastIntCount = 0;
    if (Speed >= 0 && FileOK)
    {
        TotalBytesRead = 0;
        WavFile.seek(DataStart);
        BackReady = false;
        BackRequested = false;
        ReadFile();       // erster Fuellvorgang bleibt synchron (einmalig, unkritisch)
        RequestBackFill(); // naechsten Block schon jetzt im Hintergrund anfordern
    }
    Count = 0;
    SpeedUpCount = 0;
    TimeElapsed = 0;
    TimeLeft = PlayingTime;
}

void XT_Wav_Class::NextSample(int16_t *Left, int16_t *Right)
{
    if (!FileOK || SamplesBufferIsEmpty)
    {
        *Left = 0;
        *Right = 0;
        return;
    }

    float step = IncreaseBy * Speed;
    Count += step;
    uint32_t advanceSamples = uint32_t(Count);
    if (advanceSamples > 0)
    {
        Count -= advanceSamples;
        SamplesDataIdx += size_t(advanceSamples) * BytesPerSample;
    }

    // Mindestens ein volles Sample (BytesPerSample Bytes) muss im Puffer
    // verfuegbar sein, nicht nur irgendein Byte - sonst koennen
    // Samples[SamplesDataIdx+1..+3] hinter LastNumBytesRead alte/fremde Daten
    // aus einem frueheren Fuellvorgang lesen (Knackser).
    if (SamplesDataIdx + BytesPerSample > (size_t)LastNumBytesRead)
    {
        // Kein synchrones SD-Warten im Normalfall - der Hintergrund-Task
        // haelt BackSamples i.d.R. schon bereit, dann reicht ein guenstiger
        // memcpy-Tausch. Nur wenn der Task nicht hinterherkam, greift der
        // synchrone Pfad als Notfall-Rueckfall.
        if (BackReady)
        {
            memcpy(Samples, BackSamples, BackNumBytesRead);
            LastNumBytesRead = BackNumBytesRead;
            SamplesDataIdx = 0;
            SamplesBufferIsEmpty = (LastNumBytesRead == 0);
            BackReady = false;
            BackRequested = false;
        }
        else
        {
            XT_I2S_Class::NoteSdUnderrun();
            ReadFile();
        }

        if (SamplesBufferIsEmpty)
        {
            *Left = 0;
            *Right = 0;
            Playing = false;
            TimeLeft = 0;
            return;
        }
        RequestBackFill(); // gleich den naechsten Block im Hintergrund nachladen
    }

    if (NumChannels == 2)
    {
        *Left = (int16_t)((uint8_t)Samples[SamplesDataIdx] | ((int16_t)Samples[SamplesDataIdx + 1] << 8));
        *Right = (int16_t)((uint8_t)Samples[SamplesDataIdx + 2] | ((int16_t)Samples[SamplesDataIdx + 3] << 8));
    }
    else if (NumChannels == 1)
    {
        *Left = (int16_t)((uint8_t)Samples[SamplesDataIdx] | ((int16_t)Samples[SamplesDataIdx + 1] << 8));
        *Right = *Left;
    }

    SetVolume(Left, Right, Volume);

    if (TotalBytesRead >= DataSize && BackNumBytesRead == 0 && !BackReady && SamplesDataIdx + BytesPerSample >= LastNumBytesRead)
    {
        Count = 0;
        SamplesDataIdx = 0;
        Playing = false;
        TimeLeft = 0;
        return;
    }

    uint32_t playedBytes = (TotalBytesRead - LastNumBytesRead) + SamplesDataIdx;
    TimeElapsed = (playedBytes * 1000u) / (SampleRate * BytesPerSample);
    TimeLeft = (PlayingTime > TimeElapsed) ? (PlayingTime - TimeElapsed) : 0;
}

/**********************************************************************************************************************/
/* XT_I2S_Class                                                                                                       */
/**********************************************************************************************************************/

XT_I2S_Class::XT_I2S_Class(uint8_t LRCLKPin, uint8_t BCLKPin, uint8_t I2SOutPin, i2s_port_t PassedPortNum)
{
    // Set up I2S config structure
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = SAMPLES_PER_SEC;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 8;
    i2s_config.dma_buf_len = 256;
    i2s_config.use_apll = 0;
    i2s_config.tx_desc_auto_clear = true;
    i2s_config.fixed_mclk = -1;

    // Set up I2S pin config structure
    pin_config.bck_io_num = BCLKPin;
    pin_config.ws_io_num = LRCLKPin;
    pin_config.data_out_num = I2SOutPin;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;

    PortNum = PassedPortNum;

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(PortNum, &pin_config);

    // init ringbuffer synchronization primitives and start consumer task
		taskENTER_CRITICAL(&s_ringInitMux);
		if (ringMutex == nullptr) ringMutex = xSemaphoreCreateMutex();
		if (slotCountSem == nullptr) slotCountSem = xSemaphoreCreateCounting(RING_SLOTS, 0);
		if (freeSlotSem == nullptr) freeSlotSem = xSemaphoreCreateCounting(RING_SLOTS, RING_SLOTS);
		// Punkt 1: Playlist-Mutex (rekursiv, siehe PlaylistLockGuard)
		if (playlistMutex == nullptr) playlistMutex = xSemaphoreCreateRecursiveMutex();
		// Punkt 2: SD-Zugriffs-Mutex + Nachlade-Queue fuer den Hintergrund-Task
		if (sdMutex == nullptr) sdMutex = xSemaphoreCreateMutex();
		if (g_sdRefillQueue == nullptr) g_sdRefillQueue = xQueueCreate(16, sizeof(XT_Wav_Class*));
		taskEXIT_CRITICAL(&s_ringInitMux);

    // Der dritte Hintergrund-Task ("SDReader") wird bewusst NICHT hier im
    // Konstruktor des globalen I2SAudio-Objekts gestartet, obwohl die
    // Semaphoren/Queue oben schon hier angelegt werden - der Konstruktor
    // laeuft als C++-Statik-Initialisierer VOR app_main()/setup(), also
    // bevor Arduino-Core, NVS und der WiFi-Treiber initialisiert sind, und
    // ein zusaetzlicher 4-KB-Stack-Task an dieser Stelle kann WiFi.softAP()
    // zum Scheitern bringen. Der Start dieses Tasks ist daher in
    // StartSdReaderTask() ausgelagert und wird von setup() erst NACH dem
    // WLAN-AP-Start aufgerufen (siehe ESP32-RC-Sound.ino) - funktional
    // aendert sich nichts, da der Task ohnehin erst gebraucht wird, sobald
    // tatsaechlich ein Sound abgespielt wird (fruehestens 5s nach dem
    // Booten, siehe loop()), nur der Zeitpunkt des Task-Starts verschiebt
    // sich hinter die kritische WLAN-Initialisierungsphase.

    // Speichere Instanz-Pointer für Producer Task
    s_pI2SAudioInstance = this;

    if (i2sConsumerTaskHandle == nullptr)
    {
        // Stack size 4096 sollte angepasst werden falls nötig
        xTaskCreatePinnedToCore(
            [](void *param) {
                // Consumer task lambda wrapper
                size_t bytesWritten = 0;
                for (;;)
                {
                    // Warte auf belegten Slot
                    if (xSemaphoreTake(slotCountSem, portMAX_DELAY) == pdTRUE)
                    {
                        uint32_t idx;
                        // kurz kritischer Abschnitt um Tail zu holen und atomar zu inkrementieren
                        xSemaphoreTake(ringMutex, portMAX_DELAY);
                        idx = ringTail & (RING_SLOTS - 1);
                        // Note: Do NOT advance tail here; advance after successful write to I2S to avoid losing slot on failure
                        xSemaphoreGive(ringMutex);

                        // Schreib direkt aus ringBuffer[idx]
                        esp_err_t res = i2s_write(I2S_NUM_0, (const void*)ringBuffer[idx], SLOT_BYTES, &bytesWritten, pdMS_TO_TICKS(100));
                        if (res != ESP_OK || bytesWritten != SLOT_BYTES)
                        {
                            // Schreibfehler: logge und trotzdem geben wir Slot frei, damit Consumer kann fortfahren.
                            Serial.print("i2s_write failed or partial write, res=");
                            Serial.print((int)res);
                            Serial.print(" bytesWritten=");
                            Serial.println(bytesWritten);
                            // continue: free slot below
                        }

                        // Slot freigeben und Tail-Index jetzt atomar inkrementieren
                        xSemaphoreTake(ringMutex, portMAX_DELAY);
                        ringTail = (ringTail + 1) & (RING_SLOTS - 1);
                        xSemaphoreGive(ringMutex);

                        xSemaphoreGive(freeSlotSem);
                    }
                }
            },
            "I2SConsumer",
            4096,
            nullptr,
            2,
            &i2sConsumerTaskHandle,
            1);
    }

    // Producer Task: Füllt kontinuierlich den Ringbuffer
    if (i2sProducerTaskHandle == nullptr)
    {
        xTaskCreatePinnedToCore(
            [](void *param) {
                // Producer task - füllt kontinuierlich den Ringbuffer
                for (;;)
                {
                    if (s_pI2SAudioInstance != nullptr)
                    {
                        ((XT_I2S_Class*)s_pI2SAudioInstance)->FillBuffer();
                    }
                    // Yield kurz, damit Consumer Task auch ausführen kann
                    // 1ms delay für optimale Balance zwischen Producer und Consumer
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            },
            "I2SProducer",
            4096,
            nullptr,
            2,
            &i2sProducerTaskHandle,
            1);
    }
}

XT_I2S_Class::~XT_I2S_Class()
{
    // stop consumer task
    if (i2sConsumerTaskHandle != nullptr)
    {
        vTaskDelete(i2sConsumerTaskHandle);
        i2sConsumerTaskHandle = nullptr;
    }

    // stop producer task
    if (i2sProducerTaskHandle != nullptr)
    {
        vTaskDelete(i2sProducerTaskHandle);
        i2sProducerTaskHandle = nullptr;
    }

    if (ringMutex) { vSemaphoreDelete(ringMutex); ringMutex = nullptr; }
    if (slotCountSem) { vSemaphoreDelete(slotCountSem); slotCountSem = nullptr; }
    if (freeSlotSem) { vSemaphoreDelete(freeSlotSem); freeSlotSem = nullptr; }

    // SD-Lese-Task stoppen (Punkt 2)
    if (sdReaderTaskHandle != nullptr) { vTaskDelete(sdReaderTaskHandle); sdReaderTaskHandle = nullptr; }
    if (g_sdRefillQueue) { vQueueDelete(g_sdRefillQueue); g_sdRefillQueue = nullptr; }
    if (sdMutex) { vSemaphoreDelete(sdMutex); sdMutex = nullptr; }
    if (playlistMutex) { vSemaphoreDelete(playlistMutex); playlistMutex = nullptr; }

    s_pI2SAudioInstance = nullptr;

    i2s_driver_uninstall(PortNum); // Free resources
}

/**********************************************************************************************************************/
/* Playlist / playback control (unmodified)                                                                            */
/**********************************************************************************************************************/

void XT_I2S_Class::Play(XT_PlayListItem_Class *Sound)
{
    Play(Sound, true, -1);
}

void XT_I2S_Class::Play(XT_PlayListItem_Class *Sound, bool Mix)
{
    Play(Sound, Mix, -1);
}

void XT_I2S_Class::Play(XT_PlayListItem_Class *Sound, bool Mix, int16_t TheVolume)
{
    // Debug-Ausgaben VOR dem Lock (betreffen nicht die Playlist-Zeiger) und
    // hinter einem Schalter statt bei jedem einzelnen Play()-Aufruf
    // unbedingt auf Serial zu schreiben - sonst haelt der Playlist-Mutex
    // laenger als noetig, und bei haeufigen Sound-Triggern (z.B.
    // Tippbetrieb) haeuft sich unnoetige Serial-Last an.
    if (g_audioDebug) {
        Serial.print("Play() called for item: "); Serial.println((uintptr_t)Sound);
        XT_Wav_Class *wav = dynamic_cast<XT_Wav_Class *>(reinterpret_cast<XT_Wav_Class *>(Sound));
        if (wav != nullptr) {
            Serial.print("  WAV FileName: ");
            Serial.println(wav->FileName);
            Serial.print("  WAV FileOK: "); Serial.println(wav->FileOK);
        }
    }

    PlaylistLockGuard lock; // Punkt 1: schuetzt gegen den I2SProducer-Task (MixSamples)
    if (AlreadyPlaying(Sound))
        RemoveFromPlayList(Sound);
    if (Mix == false)
        StopAllSounds();

    Sound->NewSound = true;
    Sound->RepeatIdx = Sound->Repeat;
    if (TheVolume >= 0)
        Sound->Volume = TheVolume;

    Sound->Init();

    if (FirstPlayListItem == nullptr)
    {
        FirstPlayListItem = Sound;
        LastPlayListItem = Sound;
    }
    else
    {
        LastPlayListItem->NextItem = Sound;
        Sound->PreviousItem = LastPlayListItem;
        LastPlayListItem = Sound;
    }
    Sound->Playing = true;
}

void XT_I2S_Class::RemoveFromPlayList(XT_PlayListItem_Class *ItemToRemove)
{
    PlaylistLockGuard lock;
    if (ItemToRemove->PreviousItem != nullptr)
        ItemToRemove->PreviousItem->NextItem = ItemToRemove->NextItem;
    else
        FirstPlayListItem = ItemToRemove->NextItem;
    if (ItemToRemove->NextItem != nullptr)
        ItemToRemove->NextItem->PreviousItem = ItemToRemove->PreviousItem;
    else
        LastPlayListItem = ItemToRemove->PreviousItem;

    ItemToRemove->PreviousItem = nullptr;
    ItemToRemove->NextItem = nullptr;
}

bool XT_I2S_Class::AlreadyPlaying(XT_PlayListItem_Class *Item)
{
    PlaylistLockGuard lock;
    XT_PlayListItem_Class *PlayItem;
    PlayItem = FirstPlayListItem;
    while (PlayItem != nullptr)
    {
        if (PlayItem == Item)
            return true;
        PlayItem = PlayItem->NextItem;
    }
    return false;
}

void XT_I2S_Class::StopAllSounds()
{
    PlaylistLockGuard lock;
    XT_PlayListItem_Class *PlayItem;
    PlayItem = FirstPlayListItem;
    while (PlayItem != nullptr)
    {
        PlayItem->Playing = false;
        RemoveFromPlayList(PlayItem);
        PlayItem = FirstPlayListItem;
    }
    FirstPlayListItem = nullptr;
}

void XT_I2S_Class::Stop(XT_PlayListItem_Class *Sound)
{
    PlaylistLockGuard lock;
    Sound->Playing = false;
    // Sound koennte bereits selbst aus der Playliste entfernt worden sein
    // (z.B. kurzer Tipp-Sound, der von selbst zu Ende gespielt hat, bevor
    // der Taster losgelassen wird -> handleSound() ruft Stop() dann ein
    // zweites Mal auf). Ohne diese Pruefung wuerden RemoveFromPlayList()'s
    // Faelle "PreviousItem/NextItem == nullptr" faelschlich als "einziger
    // Listeneintrag" interpretiert und FirstPlayListItem/LastPlayListItem
    // bedingungslos auf nullptr gesetzt - das kappt die gesamte aktive
    // Playliste (u.a. den Motorsound) unabhaengig vom tatsaechlichen
    // Listeninhalt. Nur entfernen, wenn der Sound wirklich noch verlinkt ist.
    if (AlreadyPlaying(Sound))
        RemoveFromPlayList(Sound);
}

int16_t CheckTopBottomedOut(int32_t Sample)
{
    if (Sample > 32767)
        return 32767;
    if (Sample < -32768)
        return -32768;
    return Sample;
}

// ── Diagnose-Zaehler (Punkt 2/16) ────────────────────────────────────────
void XT_I2S_Class::NoteSdUnderrun() { s_sdUnderrunCount++; }
uint32_t XT_I2S_Class::GetSdUnderrunCount() { return s_sdUnderrunCount; }

// SD-Lese-Task wird hier gestartet statt im Konstruktor - aufgerufen von
// setup() erst NACH dem WLAN-AP-Start (siehe Kommentar am Konstruktor
// weiter oben). sdMutex/g_sdRefillQueue selbst bleiben im Konstruktor
// angelegt (unkritisch klein) - nur der eigentliche Task (4 KB Stack,
// laeuft sofort aktiv) startet spaeter. Idempotent (Handle-Check), falls
// versehentlich mehrfach aufgerufen.
void XT_I2S_Class::StartSdReaderTask()
{
    if (sdReaderTaskHandle == nullptr)
    {
        xTaskCreatePinnedToCore(sdReaderTaskFn, "SDReader", 4096, nullptr, 1, &sdReaderTaskHandle, 1);
    }
}


int16_t ApplyCompressor(int32_t sample, int threshold = 12000, float ratio = 4.0f) {
    int sign = (sample >= 0) ? 1 : -1;
    int absSample = std::abs(sample);

    if (absSample > threshold) {
        int excess = absSample - threshold;
        absSample = threshold + int(excess / ratio);
    }

    return int16_t(std::clamp(sign * absSample, -32768, 32767));
}

uint32_t XT_I2S_Class::MixSamples()
{
    PlaylistLockGuard lock; // Punkt 1: unkritisch jetzt, da NextSample() dank Punkt 2
                             // nicht mehr synchron auf die SD-Karte wartet (kurze Sperrzeit)
    XT_PlayListItem_Class *PlayItem, *NextPlayItem;
    uint32_t MixedSample;
    int16_t LeftSample, RightSample;
    int32_t LeftMix, RightMix;
		int activeSources = 0;

    PlayItem = FirstPlayListItem;
    LeftMix = 0;
    RightMix = 0;
    while (PlayItem != nullptr)
    {
        if (PlayItem->Playing)
        {
            PlayItem->NextSample(&LeftSample, &RightSample);
            if (PlayItem->Filter != 0)
                PlayItem->Filter->FilterWave(&LeftSample, &RightSample);
            SetVolume(&LeftSample, &RightSample, PlayItem->Volume);
            LeftMix += LeftSample;
            RightMix += RightSample;
						activeSources++;
        }

        NextPlayItem = PlayItem->NextItem;
        if (PlayItem->Playing == false)
        {
            if (PlayItem->RepeatForever)
            {
                PlayItem->Init();
                PlayItem->Playing = true;
            }
            else
            {
                if (PlayItem->RepeatIdx > 0)
                {
                    PlayItem->RepeatIdx--;
                    PlayItem->Init();
                    PlayItem->Playing = true;
                }
                else
                    RemoveFromPlayList(PlayItem);
            }
        }
        PlayItem = NextPlayItem;
    }

    // Skalierung bei mehreren Quellen
		//static float PrevMixFactor = 1.0f;
		//float TargetMixFactor = 1.0f / std::max(1, activeSources);
		//PrevMixFactor = PrevMixFactor * 0.9f + TargetMixFactor * 0.1f;

		//LeftMix *= PrevMixFactor;
		//RightMix *= PrevMixFactor;


    // Lautstärke auf Gesamtmix anwenden
    //int16_t Left = int16_t(LeftMix);
    //int16_t Right = int16_t(RightMix);
    //SetVolume(&Left, &Right, Volume);

    // Begrenzung
    //LeftMix = uint16_t(CheckTopBottomedOut(Left));
    //RightMix = uint16_t(CheckTopBottomedOut(Right));
		LeftMix = ApplyCompressor(LeftMix);
   	RightMix = ApplyCompressor(RightMix);


    MixedSample = (LeftMix << 16) | (RightMix);
    return MixedSample;
}


/**********************************************************************************************************************/
/* Ringbuffer helper functions                                                                                         */
/**********************************************************************************************************************/

// Schreibe kompletten Slot (bytes == SLOT_BYTES) in Ring.
// waitTicks == 0 -> non-blocking, sofort false wenn kein freier Slot
bool RingWriteSlot(const void *src, size_t bytes, TickType_t waitTicks = pdMS_TO_TICKS(50))
{
    if (bytes != SLOT_BYTES) return false;
    // try to take a free slot
    if (waitTicks == 0)
    {
        if (xSemaphoreTake(freeSlotSem, 0) != pdTRUE) return false;
    }
    else
    {
        if (xSemaphoreTake(freeSlotSem, waitTicks) != pdTRUE) return false;
    }

    // reserve index but DO NOT advance head until copy finished
    uint32_t idx;
    xSemaphoreTake(ringMutex, portMAX_DELAY);
    idx = ringHead & (RING_SLOTS - 1);
    // temporarily mark slot reserved by incrementing a local headReserve variable (we'll commit after memcpy)
    uint32_t nextHead = (ringHead + 1) & (RING_SLOTS - 1);
    // do not write ringHead yet, to avoid marking slot as available for consumer before data copied
    xSemaphoreGive(ringMutex);

    // copy data into reserved slot
    memcpy((void*)ringBuffer[idx], src, SLOT_BYTES);

    // commit the head index so consumer can see the slot
    xSemaphoreTake(ringMutex, portMAX_DELAY);
    ringHead = nextHead;
    xSemaphoreGive(ringMutex);

    xSemaphoreGive(slotCountSem);
    return true;
}

/**********************************************************************************************************************/
/* FillBuffer: Producer-seite (schreibt einen Slot in den Ring)                                                       */
/* Hinweis: Fülle häufiger oder starte einen Producer-Task, falls kontinuierliche Produktion nötig                     */
/**********************************************************************************************************************/
void XT_I2S_Class::FillBuffer()
{
    uint32_t tmpSlot[SLOT_WORDS];

    for (size_t i = 0; i < SLOT_WORDS; ++i)
    {
        tmpSlot[i] = MixSamples();
    }

    // Schreibe kompletten Slot (blockierend mit Timeout)
    bool ok = RingWriteSlot(tmpSlot, SLOT_BYTES, pdMS_TO_TICKS(100));
    if (!ok)
    {
        // Kein freier Slot -> Unterlauf/Verwerfen. Optional: Statistik/Logging.
        // Aufzeichnen eines Counters wäre hier hilfreich.
        Serial.println("FillBuffer: no free slot, dropping buffer");
    }
}