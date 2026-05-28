#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#define FORMAT_LITTLEFS_IF_FAILED true

char FileBuffer[32];
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
    FS_Open();
    readFile(LittleFS, "/SSID.txt");
    FS_Close();
    return FileBuffer;
}

String PASSWORD_Lesen()
{
    FS_Open();
    readFile(LittleFS, "/PASSWORD.txt");
    FS_Close();
    return FileBuffer;
}

void SSID_Schreiben(String Wert)
{
    char arr[33];
    Wert.toCharArray(arr, sizeof(arr));
    FS_Open();
    writeFile(LittleFS, "/SSID.txt", arr);
    FS_Close();

    Serial.println(SSID_Lesen());
    Serial.println();
}

void PASSWORD_Schreiben(String Wert)
{
    char arr[33];
    Wert.toCharArray(arr, sizeof(arr));
    FS_Open();
    writeFile(LittleFS, "/PASSWORD.txt", arr);
    FS_Close();

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

// Persistentes Fehlerlog (Ringpuffer, max. 20 Einträge)
constexpr uint8_t ERROR_LOG_MAX_ENTRIES = 20;
constexpr const char *ERROR_LOG_PATH = "/error_log.txt";
constexpr unsigned long ERROR_LOG_DEDUP_WINDOW_DEFAULT_MS = 30000UL; // 30s: gleiche Fehler zusammenfassen
constexpr unsigned long ERROR_LOG_DEDUP_WINDOW_MIN_MS = 1000UL;
constexpr unsigned long ERROR_LOG_DEDUP_WINDOW_MAX_MS = 600000UL;

static unsigned long g_errorLogDedupWindowMs = ERROR_LOG_DEDUP_WINDOW_DEFAULT_MS;

static String g_lastErrorLogMsg = "";
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

inline String _sanitizeLogLine(String msg)
{
    msg.replace("\r", " ");
    msg.replace("\n", " ");
    msg.trim();
    if (msg.length() == 0)
        return "(leer)";
    if (msg.length() > 96)
        return msg.substring(0, 96);
    return msg;
}

void ErrorLogInit()
{
    FS_Open();
    File f = LittleFS.open(ERROR_LOG_PATH, FILE_APPEND);
    if (f) f.close();
    FS_Close();
}

void ErrorLogAdd(String message)
{
    String cleanMessage = _sanitizeLogLine(message);
    unsigned long nowMs = millis();

    // Schutz vor Dauerfehlern: identische Meldung im Zeitfenster nicht erneut schreiben.
    if (cleanMessage == g_lastErrorLogMsg &&
        (nowMs - g_lastErrorLogWriteMs) < g_errorLogDedupWindowMs)
    {
        g_lastErrorLogSuppressed++;
        return;
    }

    String lines[ERROR_LOG_MAX_ENTRIES];
    uint8_t count = 0;

    FS_Open();

    File in = LittleFS.open(ERROR_LOG_PATH, FILE_READ);
    if (in)
    {
        while (in.available())
        {
            String line = in.readStringUntil('\n');
            line.trim();
            if (line.length() == 0)
                continue;

            if (count < ERROR_LOG_MAX_ENTRIES)
            {
                lines[count++] = line;
            }
            else
            {
                for (uint8_t i = 1; i < ERROR_LOG_MAX_ENTRIES; i++)
                    lines[i - 1] = lines[i];
                lines[ERROR_LOG_MAX_ENTRIES - 1] = line;
            }
        }
        in.close();
    }

    // Falls Wiederholungen unterdrückt wurden, als Sammelzeile dokumentieren.
    if (g_lastErrorLogSuppressed > 0)
    {
        String summary = "[" + String(nowMs) + " ms] " + g_lastErrorLogMsg +
                         " (" + String(g_lastErrorLogSuppressed) + "x wiederholt, unterdrueckt)";
        if (count < ERROR_LOG_MAX_ENTRIES)
        {
            lines[count++] = summary;
        }
        else
        {
            for (uint8_t i = 1; i < ERROR_LOG_MAX_ENTRIES; i++)
                lines[i - 1] = lines[i];
            lines[ERROR_LOG_MAX_ENTRIES - 1] = summary;
        }
        g_lastErrorLogSuppressed = 0;
    }

    String entry = "[" + String(nowMs) + " ms] " + cleanMessage;
    if (count < ERROR_LOG_MAX_ENTRIES)
    {
        lines[count++] = entry;
    }
    else
    {
        for (uint8_t i = 1; i < ERROR_LOG_MAX_ENTRIES; i++)
            lines[i - 1] = lines[i];
        lines[ERROR_LOG_MAX_ENTRIES - 1] = entry;
    }

    File out = LittleFS.open(ERROR_LOG_PATH, "w");
    if (out)
    {
        for (uint8_t i = 0; i < count; i++)
            out.println(lines[i]);
        out.close();

        g_lastErrorLogMsg = cleanMessage;
        g_lastErrorLogWriteMs = nowMs;
    }

    FS_Close();
}

void ErrorLogClear()
{
    FS_Open();
    File f = LittleFS.open(ERROR_LOG_PATH, FILE_WRITE);
    if (f) f.close();
    FS_Close();

    g_lastErrorLogMsg = "";
    g_lastErrorLogWriteMs = 0;
    g_lastErrorLogSuppressed = 0;
}

void ErrorLogPrint()
{
    FS_Open();
    File file = LittleFS.open(ERROR_LOG_PATH, FILE_READ);
    if (!file)
    {
        Serial.println("Fehlerlog: leer.");
        FS_Close();
        return;
    }

    Serial.println("----- Fehlerlog (letzte 20) -----");
    uint8_t idx = 1;
    bool hasLine = false;
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
            continue;
        hasLine = true;
        Serial.printf("%02u) %s\n", idx++, line.c_str());
    }
    if (!hasLine)
        Serial.println("(leer)");
    Serial.println("-------------------------------");

    file.close();
    FS_Close();
}

#endif