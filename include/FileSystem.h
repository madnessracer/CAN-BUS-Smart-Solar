#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <UnixTime.h>

#define FORMAT_LITTLEFS_IF_FAILED true

Preferences nvsPrefs;
char FileBuffer[64];
String SSID;
String PASSWORD;

void FS_Open()
{
    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED))
    {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    delay(100);
}

void FS_Close()
{
    LittleFS.end();
    delay(100);
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels)
            {
                listDir(fs, file.path(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void createDir(fs::FS &fs, const char *path)
{
    Serial.printf("Creating Dir: %s\n", path);
    if (fs.mkdir(path))
    {
        Serial.println("Dir created");
    }
    else
    {
        Serial.println("mkdir failed");
    }
}

void removeDir(fs::FS &fs, const char *path)
{
    Serial.printf("Removing Dir: %s\n", path);
    if (fs.rmdir(path))
    {
        Serial.println("Dir removed");
    }
    else
    {
        Serial.println("rmdir failed");
    }
}

void readFile(fs::FS &fs, const char *path)
{
    if (!fs.exists(path))
    {
        FileBuffer[0] = '\0';
        return;
    }

    File file = fs.open(path);

    if (!file || file.isDirectory())
    {
        Serial.println("- failed to open file for reading");
        return;
    }

    while (file.available())
    {
        int l = file.readBytesUntil('\n', FileBuffer, sizeof(FileBuffer));
        FileBuffer[l] = 0;
    }

    file.close();
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    //Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
       // Serial.println("- file written");
    }
    else
    {
        Serial.println("- write failed");
    }
    file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file)
    {
        Serial.println("- failed to open file for appending");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- message appended");
    }
    else
    {
        Serial.println("- append failed");
    }
    file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2))
    {
        Serial.println("- file renamed");
    }
    else
    {
        Serial.println("- rename failed");
    }
}

void deleteFile(fs::FS &fs, const char *path)
{
    Serial.printf("Deleting file: %s\r\n", path);
    if (fs.remove(path))
    {
        Serial.println("- file deleted");
    }
    else
    {
        Serial.println("- delete failed");
    }
}

bool OTA_UpdatePendingReset()
{
    FS_Open();
    bool pending = LittleFS.exists("/ota_update_reset.flag");
    FS_Close();
    return pending;
}

void OTA_SetPendingReset()
{
    FS_Open();
    writeFile(LittleFS, "/ota_update_reset.flag", "1");
    FS_Close();
}

void OTA_ClearPendingReset()
{
    FS_Open();
    if (LittleFS.exists("/ota_update_reset.flag"))
    {
        deleteFile(LittleFS, "/ota_update_reset.flag");
    }
    FS_Close();
}

void testFileIO(fs::FS &fs, const char *path)
{
    Serial.printf("Testing file I/O with %s\r\n", path);

    static uint8_t buf[512];
    size_t len = 0;
    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }

    size_t i;
    Serial.print("- writing");
    uint32_t start = millis();
    for (i = 0; i < 2048; i++)
    {
        if ((i & 0x001F) == 0x001F)
        {
            Serial.print(".");
        }
        file.write(buf, 512);
    }
    Serial.println("");
    uint32_t end = millis() - start;
    Serial.printf(" - %u bytes written in %u ms\r\n", 2048 * 512, end);
    file.close();

    file = fs.open(path);
    start = millis();
    end = start;
    i = 0;
    if (file && !file.isDirectory())
    {
        len = file.size();
        size_t flen = len;
        start = millis();
        Serial.print("- reading");
        while (len)
        {
            size_t toRead = len;
            if (toRead > 512)
            {
                toRead = 512;
            }
            file.read(buf, toRead);
            if ((i++ & 0x001F) == 0x001F)
            {
                Serial.print(".");
            }
            len -= toRead;
        }
        Serial.println("");
        end = millis() - start;
        Serial.printf("- %u bytes read in %u ms\r\n", flen, end);
        file.close();
    }
    else
    {
        Serial.println("- failed to open file for reading");
    }
}

String SSID_Lesen()
{
    if (!nvsPrefs.begin("wifi", true))
    {
        Serial.println("NVS open failed for read");
        return String();
    }

    String value = nvsPrefs.getString("ssid", "");
    nvsPrefs.end();
    return value;
}

String PASSWORD_Lesen()
{
    if (!nvsPrefs.begin("wifi", true))
    {
        Serial.println("NVS open failed for read");
        return String();
    }

    String value = nvsPrefs.getString("password", "");
    nvsPrefs.end();
    return value;
}

void SSID_Schreiben(String Wert)
{
    if (!nvsPrefs.begin("wifi", false))
    {
        Serial.println("NVS open failed for write");
        return;
    }

    nvsPrefs.putString("ssid", Wert);
    nvsPrefs.end();

    Serial.println(SSID_Lesen());
    Serial.println();
}

void PASSWORD_Schreiben(String Wert)
{
    if (!nvsPrefs.begin("wifi", false))
    {
        Serial.println("NVS open failed for write");
        return;
    }

    nvsPrefs.putString("password", Wert);
    nvsPrefs.end();

    Serial.println(PASSWORD_Lesen());
    Serial.println();
}

// Firmware-Version-Zähler (erhöht bei OTA-Update)
uint32_t getFirmwareVersion()
{
    FS_Open();
    if (!LittleFS.exists("/firmware_version.txt"))
    {
        writeFile(LittleFS, "/firmware_version.txt", "0");
    }
    readFile(LittleFS, "/firmware_version.txt");
    FS_Close();
    uint32_t version = atoi(FileBuffer);
    return version;
}

void incrementFirmwareVersion()
{
    uint32_t version = getFirmwareVersion() + 1;
    char buf[16];
    sprintf(buf, "%lu", version);
    FS_Open();
    writeFile(LittleFS, "/firmware_version.txt", buf);
    FS_Close();
    //Serial.printf("Firmware-Version erhöht auf: %lu\n", version);
}

// Boot-Zähler (erhöht bei jedem Boot)
uint32_t getBootCount()
{
    FS_Open();
    readFile(LittleFS, "/boot_count.txt");
    FS_Close();
    uint32_t count = atoi(FileBuffer);
    return count > 0 ? count : 0; // Default 0
}

void incrementBootCount()
{
    uint32_t count = getBootCount() + 1;
    char buf[16];
    sprintf(buf, "%lu", count);
    FS_Open();
    writeFile(LittleFS, "/boot_count.txt", buf);
    FS_Close();
    //Serial.printf("Boot-Zähler erhöht auf: %lu\n", count);
}

// Fehlerlog im RAM (flüchtig – geht beim Reboot verloren)
constexpr uint8_t ERROR_LOG_MAX_ENTRIES = 20;
constexpr uint8_t ERROR_LOG_ENTRY_SIZE  = 128; // feste Puffergröße pro Eintrag
constexpr unsigned long ERROR_LOG_DEDUP_WINDOW_DEFAULT_MS = 30000UL; // 30s: gleiche Fehler zusammenfassen
constexpr unsigned long ERROR_LOG_DEDUP_WINDOW_MIN_MS = 1000UL;
constexpr unsigned long ERROR_LOG_DEDUP_WINDOW_MAX_MS = 600000UL;

// RAM-Ringpuffer: feste char-Arrays – keine Heap-Fragmentierung
static char g_errorLogRam[ERROR_LOG_MAX_ENTRIES][ERROR_LOG_ENTRY_SIZE];
static uint8_t g_errorLogRamCount = 0;

static unsigned long g_errorLogDedupWindowMs = ERROR_LOG_DEDUP_WINDOW_DEFAULT_MS;
static char g_lastErrorLogMsg[96] = {};
static unsigned long g_lastErrorLogWriteMs = 0;
static uint16_t g_lastErrorLogSuppressed = 0;

unsigned long ErrorLogGetDedupWindowMs()
{
    return g_errorLogDedupWindowMs;
}

void ErrorLogSetDedupWindowMs(unsigned long ms)
{
    if (ms < ERROR_LOG_DEDUP_WINDOW_MIN_MS)
        ms = ERROR_LOG_DEDUP_WINDOW_MIN_MS;
    if (ms > ERROR_LOG_DEDUP_WINDOW_MAX_MS)
        ms = ERROR_LOG_DEDUP_WINDOW_MAX_MS;
    g_errorLogDedupWindowMs = ms;
}

inline void ErrorLogResetSuppression()
{
    g_lastErrorLogMsg[0] = '\0';
    g_lastErrorLogWriteMs = 0;
    g_lastErrorLogSuppressed = 0;
}

// Bereinigt msg in dst (max dstSize-1 Zeichen): ersetzt \r\n, trimmt, kürzt
static void _sanitizeLogLine(const char *src, char *dst, size_t dstSize)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dstSize - 1; i++) {
        char c = src[i];
        if (c == '\r' || c == '\n') c = ' ';
        dst[j++] = c;
    }
    dst[j] = '\0';
    // rtrim
    while (j > 0 && dst[j-1] == ' ') dst[--j] = '\0';
    // ltrim
    size_t start = 0;
    while (dst[start] == ' ') start++;
    if (start > 0) memmove(dst, dst + start, j - start + 1);
    if (dst[0] == '\0') strncpy(dst, "(leer)", dstSize - 1);
}

// Schreibt Zeitstempel in buf (mindestens 24 Bytes)
static void _makeTimestamp(char *buf, size_t bufSize)
{
    if (UnixTime::isSynced()) {
        time_t t = UnixTime::now();
        struct tm ti;
        localtime_r(&t, &ti);
        snprintf(buf, bufSize, "%02d.%02d.%04d %02d:%02d:%02d",
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        unsigned long s = millis() / 1000UL;
        unsigned long d = s / 86400UL; s %= 86400UL;
        unsigned long h = s / 3600UL;  s %= 3600UL;
        unsigned long m = s / 60UL;    s %= 60UL;
        snprintf(buf, bufSize, "Uptime %lud %02lu:%02lu:%02lu", d, h, m, s);
    }
}

static void _errorLogRamAppend(const char *line)
{
    if (g_errorLogRamCount < ERROR_LOG_MAX_ENTRIES)
    {
        strncpy(g_errorLogRam[g_errorLogRamCount++], line, ERROR_LOG_ENTRY_SIZE - 1);
        g_errorLogRam[g_errorLogRamCount - 1][ERROR_LOG_ENTRY_SIZE - 1] = '\0';
    }
    else
    {
        memmove(g_errorLogRam[0], g_errorLogRam[1],
                (ERROR_LOG_MAX_ENTRIES - 1) * ERROR_LOG_ENTRY_SIZE);
        strncpy(g_errorLogRam[ERROR_LOG_MAX_ENTRIES - 1], line, ERROR_LOG_ENTRY_SIZE - 1);
        g_errorLogRam[ERROR_LOG_MAX_ENTRIES - 1][ERROR_LOG_ENTRY_SIZE - 1] = '\0';
    }
}

void ErrorLogInit()
{
    g_errorLogRamCount = 0;
    memset(g_errorLogRam, 0, sizeof(g_errorLogRam));
    g_lastErrorLogMsg[0] = '\0';
    g_lastErrorLogWriteMs = 0;
    g_lastErrorLogSuppressed = 0;
}

// Forward-Deklaration
void ErrorLogSave();

void ErrorLogAdd(String message)
{
    char cleanMessage[96];
    _sanitizeLogLine(message.c_str(), cleanMessage, sizeof(cleanMessage));
    unsigned long nowMs = millis();

    // Schutz vor Dauerfehlern: identische Meldung im Zeitfenster nicht erneut schreiben.
    if (strncmp(cleanMessage, g_lastErrorLogMsg, sizeof(g_lastErrorLogMsg)) == 0 &&
        (nowMs - g_lastErrorLogWriteMs) < g_errorLogDedupWindowMs)
    {
        g_lastErrorLogSuppressed++;
        return;
    }

    char ts[24];
    _makeTimestamp(ts, sizeof(ts));
    char entry[ERROR_LOG_ENTRY_SIZE];

    // Falls Wiederholungen unterdrückt wurden, als Sammelzeile dokumentieren.
    if (g_lastErrorLogSuppressed > 0)
    {
        snprintf(entry, sizeof(entry), "[%s] %s (%ux wiederholt)",
                 ts, g_lastErrorLogMsg, (unsigned)g_lastErrorLogSuppressed);
        _errorLogRamAppend(entry);
        g_lastErrorLogSuppressed = 0;
    }

    snprintf(entry, sizeof(entry), "[%s] %s", ts, cleanMessage);
    _errorLogRamAppend(entry);

    strncpy(g_lastErrorLogMsg, cleanMessage, sizeof(g_lastErrorLogMsg) - 1);
    g_lastErrorLogMsg[sizeof(g_lastErrorLogMsg) - 1] = '\0';
    g_lastErrorLogWriteMs = nowMs;
    ErrorLogSave();
}

void ErrorLogClear()
{
    g_errorLogRamCount = 0;
    memset(g_errorLogRam, 0, sizeof(g_errorLogRam));
    g_lastErrorLogMsg[0] = '\0';
    g_lastErrorLogWriteMs = 0;
    g_lastErrorLogSuppressed = 0;
    ErrorLogSave();
}

void ErrorLogPrint()
{
    Serial.println("----- Fehlerlog (letzte 20, RAM) -----");
    if (g_errorLogRamCount == 0)
    {
        Serial.println("(leer)");
    }
    else
    {
        for (uint8_t i = 0; i < g_errorLogRamCount; i++)
            Serial.printf("%02u) %s\n", i + 1, g_errorLogRam[i]);
    }
    Serial.println("-------------------------------");
}

// Speichert den RAM-Log auf LittleFS (binär: 1 Byte Count + Einträge)
void ErrorLogSave()
{
    FS_Open();
    File f = LittleFS.open("/errorlog.bin", FILE_WRITE);
    if (!f) { FS_Close(); return; }
    f.write(&g_errorLogRamCount, 1);
    f.write((const uint8_t *)g_errorLogRam, sizeof(g_errorLogRam));
    f.close();
    FS_Close();
}

// Lädt den gespeicherten Log vom LittleFS in den RAM-Puffer
void ErrorLogLoad()
{
    FS_Open();
    File f = LittleFS.open("/errorlog.bin", FILE_READ);
    if (!f) { FS_Close(); return; }
    if (f.size() < 1 + sizeof(g_errorLogRam)) { f.close(); FS_Close(); return; }
    f.read(&g_errorLogRamCount, 1);
    if (g_errorLogRamCount > ERROR_LOG_MAX_ENTRIES) g_errorLogRamCount = ERROR_LOG_MAX_ENTRIES;
    f.read((uint8_t *)g_errorLogRam, sizeof(g_errorLogRam));
    f.close();
    FS_Close();
}

#endif