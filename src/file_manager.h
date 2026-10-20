#pragma once

#include "config.h"

void fileManagerSetup();
void refreshFileList();
int getFileCount();
FileInfo* getFileList();

void loadFile(const char* filename);
void saveCurrentFile(bool refreshList = true);
void createNewFile();
void deriveUniqueFilename(const char* title, char* out, int maxLen, const char* extension = ".md", const char* existing = nullptr);
void updateFileTitle(const char* filename, const char* newTitle);
void deleteFile(const char* filename);

bool fileManagerSaveFailed();
const char* fileManagerSaveError();
