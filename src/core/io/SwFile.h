#pragma once
/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwCoreApplication.h"
#include "SwIODevice.h"
#include "SwDateTime.h"
#include "SwStandardLocation.h"
#include "SwCrypto.h"
#include "SwDebug.h"
#include "platform/SwPlatformSelector.h"

#include <fstream>
#include <string>
#include <sys/stat.h>
#include <iostream>
#include <stdexcept>
#include <ctime>
#include <memory>
#include <algorithm>
#include <sstream>
static constexpr const char* kSwLogCategory_SwFile = "sw.core.io.swfile";



class SwFile : public SwIODevice {
public:
    enum OpenMode {
        Read,
        Write,
        Append
    };

    SwFile(SwObject* parent = nullptr)
        : SwIODevice(parent), currentMode_(Read) {}

    explicit SwFile(const SwString& filePath, SwObject* parent = nullptr)
        : SwIODevice(parent), currentMode_(Read) {
        filePath_ = filePath;
    }

    virtual ~SwFile() {
        close();
    }


    // Définir le chemin du fichier
    void setFilePath(const SwString& filePath) {
        filePath_ = filePath;
    }

    SwString fileName() const {
        if (filePath_.isEmpty()) {
            swCError(kSwLogCategory_SwFile) << "Error: File path is empty!";
            return SwString();
        }

        std::string path = filePath_.toStdString();
        std::replace(path.begin(), path.end(), '\\', '/');
        size_t pos = path.find_last_of('/');
        return (pos == std::string::npos) ? SwString(path) : SwString(path.substr(pos + 1));
    }

    // Ouvrir un fichier
    bool open(OpenMode mode) {
        if (filePath_.isEmpty()) {
            swCError(kSwLogCategory_SwFile) << "Chemin du fichier non défini.";
        }

        std::ios::openmode openMode;
        switch (mode) {
        case Read:
            openMode = std::ios::in;
            break;
        case Write:
            openMode = std::ios::out | std::ios::trunc;
            break;
        case Append:
            openMode = std::ios::out | std::ios::app;
            break;
        default:
            swCError(kSwLogCategory_SwFile) << "Mode d'ouverture invalide.";
        }

        fileStream_.open(filePath_, openMode);
        if (fileStream_.is_open()) {
            currentMode_ = mode;
        }
        return fileStream_.is_open();
    }

    // Ouvrir un fichier en mode binaire (utile pour ecrire des fichiers non texte).
    bool openBinary(OpenMode mode) {
        if (filePath_.isEmpty()) {
            swCError(kSwLogCategory_SwFile) << "Chemin du fichier non defini.";
        }

        std::ios::openmode openMode;
        switch (mode) {
        case Read:
            openMode = std::ios::in;
            break;
        case Write:
            openMode = std::ios::out | std::ios::trunc;
            break;
        case Append:
            openMode = std::ios::out | std::ios::app;
            break;
        default:
            swCError(kSwLogCategory_SwFile) << "Mode d'ouverture invalide.";
        }

        openMode |= std::ios::binary;

        fileStream_.open(filePath_, openMode);
        if (fileStream_.is_open()) {
            currentMode_ = mode;
        }
        return fileStream_.is_open();
    }

    // Fermer le fichier
    void close() override {
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
        stopMonitoring();
    }

    // Écrire dans le fichier
    bool write(const SwString& data) {
        if (currentMode_ != Write && currentMode_ != Append) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert en mode écriture.";
        }
        fileStream_ << data;
        fileStream_.flush();
        return fileStream_.good();
    }

    bool write(const SwByteArray& data) override {
        if (currentMode_ != Write && currentMode_ != Append) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert en mode ecriture.";
        }
        if (data.size() == 0) {
            return fileStream_.good();
        }
        const char* bytes = data.constData();
        if (!bytes) {
            return fileStream_.good();
        }
        fileStream_.write(bytes, static_cast<std::streamsize>(data.size()));
        fileStream_.flush();
        return fileStream_.good();
    }

    SwString readAll() {
        if (currentMode_ != Read) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert en mode lecture.";
        }

        std::ostringstream buffer;
        buffer << fileStream_.rdbuf();
        if (fileStream_.fail() && !fileStream_.eof()) {
            return SwString();
        }
        return SwString(buffer.str());
    }

    // Vérifier si le fichier est ouvert
    bool isOpen() const override {
        return fileStream_.is_open();
    }

    SwString getDirectory() const {
        if (filePath_.isEmpty()) {
            swCError(kSwLogCategory_SwFile) << "Chemin du fichier non défini.";
            return SwString();
        }

        std::string path = filePath_.toStdString();
        std::replace(path.begin(), path.end(), '\\', '/');
        size_t pos = path.find_last_of('/');
        if (pos == std::string::npos) {
            return SwString();
        }
        return SwString(path.substr(0, pos));
    }


    static bool isFile(const SwString& path) {
        return swFilePlatform().isFile(path);
    }


    bool contains(const SwString& keyword) {
        if (currentMode_ != Read) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert en mode lecture.";
        }

        std::string line;
        fileStream_.seekg(0); // Revenir au début du fichier
        while (std::getline(fileStream_, line)) {
            if (line.find(keyword) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    SwString readLine(std::size_t lineNumber) {
        if (currentMode_ != Read) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert en mode lecture.";
        }

        std::string line;
        fileStream_.seekg(0); // Revenir au début du fichier
        for (std::size_t currentLine = 0; currentLine <= lineNumber; ++currentLine) {
                if (!std::getline(fileStream_, line)) {
                swCError(kSwLogCategory_SwFile) << "Ligne hors limites.";
            }
        }
        return line;
    }

    SwString readLine() {
        if (currentMode_ != Read) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert en mode lecture.";
        }

        std::string line;
        if (!std::getline(fileStream_, line)) {
            return "";
        }
        return line;
    }

    bool atEnd() const {
        if (!fileStream_.is_open()) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert.";
        }
        return fileStream_.eof();
    }

    SwString readChunk(std::size_t chunkSize) {
        if (currentMode_ != Read) {
            throw std::runtime_error("Fichier non ouvert en mode lecture.");
        }

        std::string buffer(chunkSize, '\0');
        fileStream_.read(&buffer[0], chunkSize);
        buffer.resize(fileStream_.gcount()); // Ajuster à la taille réelle lue

        return buffer;
    }

    void seek(std::streampos position) {
        if (!isOpen()) {
            throw std::runtime_error("Fichier non ouvert.");
        }
        fileStream_.seekg(position);
    }

    std::streampos currentPosition() {
        if (!isOpen()) {
            throw std::runtime_error("Fichier non ouvert.");
        }
        return fileStream_.tellg();
    }


    SwString readLinesInRangeLazy(std::size_t startLine, std::size_t endLine) {
        if (currentMode_ != Read) {
            throw std::runtime_error("Fichier non ouvert en mode lecture.");
        }

        if (startLine > endLine) {
            throw std::invalid_argument("La ligne de début doit être inférieure ou égale à la ligne de fin.");
        }

        if (!fileStream_.is_open()) {
            throw std::runtime_error("Le flux du fichier n'est pas ouvert.");
        }

        // Réinitialiser le flux pour éviter les problèmes d'état
        fileStream_.clear();
        fileStream_.seekg(0, std::ios::beg); // Revenir au début du fichier

        SwString result;
        std::string line;
        std::size_t currentLine = 0;

        while (std::getline(fileStream_, line)) {
            if (currentLine >= startLine && currentLine <= endLine) {
                result += line + '\n'; // Ajouter la ligne avec un saut de ligne
            }
            if (currentLine > endLine) {
                break; // On arrête dès que la plage est dépassée
            }
            ++currentLine;
        }

        return result;
    }

    static bool copy(const SwString& source, const SwString& destination, bool overwrite = true) {
        return swFilePlatform().copy(source, destination, overwrite);
    }

    static bool copyByChunk(const SwString& source, const SwString& destination,
                            bool nonBlocking = true, int chunkSize = 1024) {
        // Vérifier si le fichier source existe
        std::ifstream srcStream(source.toStdString(), std::ios::binary);
        if (!srcStream.is_open()) {
            swCError(kSwLogCategory_SwFile) << "Failed to open source file: " << source;
            return false;
        }

        // Vérifier si la destination est un répertoire
        std::string finalDestination = destination.toStdString();
        struct stat info;
        if (stat(finalDestination.c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
            size_t lastSlash = source.toStdString().find_last_of("/\\");
            std::string fileName = (lastSlash == std::string::npos) ? source.toStdString() : source.toStdString().substr(lastSlash + 1);

            if (finalDestination.back() != '/' && finalDestination.back() != '\\') {
                finalDestination += "\\";
            }
            finalDestination += fileName;
        }

        // Ouvrir le fichier de destination
        std::ofstream destStream(finalDestination, std::ios::binary | std::ios::trunc);
        if (!destStream.is_open()) {
            swCError(kSwLogCategory_SwFile) << "Failed to open destination file: " << finalDestination;
            return false;
        }

        // Utiliser le tas pour le buffer
        size_t bufferSize = chunkSize * 1024;
        std::unique_ptr<char[]> buffer(new char[bufferSize]); // Allocation sur le tas

        // Copier par blocs
        while (true) {
            srcStream.read(buffer.get(), bufferSize); // Lire un bloc
            std::streamsize bytesRead = srcStream.gcount(); // Obtenir le nombre d'octets lus
            if (bytesRead == 0) {
                break; // Rien à lire, fin de la boucle
            }
            if (nonBlocking) { SwCoreApplication::instance()->processEvent(); }

            destStream.write(buffer.get(), bytesRead); // Écriture des données
        }

        if (!destStream.good()) {
            swCError(kSwLogCategory_SwFile) << "Error occurred during file copy from " << source << " to " << finalDestination;
            return false;
        }
        return true;
    }


    bool copyByChunk(const SwString& destination, bool nonBlocking = true, int chunkSize = 1024) {
        if (filePath_.isEmpty()) {
            swCError(kSwLogCategory_SwFile) << "Source file path is not set.";
            return false;
        }

        // Vérifier si le fichier source existe
        std::ifstream srcStream(filePath_, std::ios::binary);
        if (!srcStream.is_open()) {
            swCError(kSwLogCategory_SwFile) << "Failed to open source file: " << filePath_;
            return false;
        }

        // Vérifier si la destination est un répertoire
        std::string finalDestination = destination.toStdString();
        struct stat info;
        if (stat(destination.toStdString().c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
            size_t lastSlash = filePath_.toStdString().find_last_of("/\\");
            std::string fileName = (lastSlash == std::string::npos) ? filePath_.toStdString() : filePath_.toStdString().substr(lastSlash + 1);

            if (destination.toStdString().back() != '/' && destination.toStdString().back() != '\\') {
                finalDestination += "/";
            }
            finalDestination += fileName;
        }

        // Ouvrir le fichier de destination
        std::ofstream destStream(finalDestination, std::ios::binary | std::ios::trunc);
        if (!destStream.is_open()) {
            swCError(kSwLogCategory_SwFile) << "Failed to open destination file: " << finalDestination;
            return false;
        }

        // Utiliser un buffer alloué sur le tas
        size_t bufferSize = chunkSize * 1024;
        std::unique_ptr<char[]> buffer(new char[bufferSize]);

        // Copier par blocs
        while (true) {
            srcStream.read(buffer.get(), bufferSize); // Lire un bloc
            std::streamsize bytesRead = srcStream.gcount(); // Obtenir le nombre d'octets lus
            if (bytesRead == 0) {
                break; // Rien à lire, fin de la boucle
            }

            // Écriture dans le fichier destination
            destStream.write(buffer.get(), bytesRead);
            if (!destStream.good()) {
                swCError(kSwLogCategory_SwFile) << "Write error occurred during file copy to: " << finalDestination;
                return false;
            }

            if (nonBlocking) { SwCoreApplication::instance()->processEvent(); }
        }

        // Vérifier si tout est bien écrit
        if (!destStream.good()) {
            swCError(kSwLogCategory_SwFile) << "Error occurred during file copy from " << filePath_ << " to " << finalDestination;
            return false;
        }

        swCDebug(kSwLogCategory_SwFile) << "File successfully copied from " << filePath_ << " to " << finalDestination;
        return true;
    }



    // Lire les métadonnées d'un fichier
    bool getFileMetadata(SwDateTime& creationTime, SwDateTime& lastAccessTime, SwDateTime& lastWriteTime) {
        return swFilePlatform().getFileMetadata(filePath_, creationTime, lastAccessTime, lastWriteTime);
    }

    // Modifier la date de création
    bool setCreationTime(SwDateTime creationTime) {
        return swFilePlatform().setCreationTime(filePath_, creationTime);
    }

    // Modifier la date de dernière modification
    bool setLastWriteDate(SwDateTime lastWriteTime) {
        return swFilePlatform().setLastWriteDate(filePath_, lastWriteTime);
    }

    // Modifier la date de dernier accès
    bool setLastAccessDate(SwDateTime lastAccessTime) {
        return swFilePlatform().setLastAccessDate(filePath_, lastAccessTime);
    }

    // Modifier toutes les dates (création, dernier accès, dernière modification)
    bool setAllDates(SwDateTime creationTime, SwDateTime lastAccessTime, SwDateTime lastWriteTime) {
        return swFilePlatform().setAllDates(filePath_, creationTime, lastAccessTime, lastWriteTime);
    }

    SwString fileChecksum()
    {
        SwString checksum;
        if(filePath_ != ""){
            try {
                checksum = SwCrypto::calculateFileChecksum(filePath_);
            } catch (const std::exception& ex) {
                swCError(kSwLogCategory_SwFile) << "File checksum erreur on: " << filePath_ << "\n" << ex.what();
            }
        }
        return checksum;
    }

    bool writeMetadata(const SwString& key, const SwString& value) {
        return swFilePlatform().writeMetadata(filePath_, key, value);
    }

    SwString readMetadata(const SwString& key) {
        return swFilePlatform().readMetadata(filePath_, key);
    }
signals:
    DECLARE_SIGNAL(fileChanged, const SwString&)

private:
    std::fstream fileStream_;
    OpenMode currentMode_;

};
