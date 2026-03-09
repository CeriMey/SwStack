#pragma once

/**
 * @file src/core/io/SwFile.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwFile in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the file interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwFile.
 *
 * File-oriented declarations here usually define handle ownership, access mode selection, and the
 * transfer of bytes between the framework API and operating-system services.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
 *
 */

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
    SW_OBJECT(SwFile, SwIODevice)
public:
    enum OpenMode {
        Read,
        Write,
        Append
    };

    /**
     * @brief Constructs a `SwFile` instance.
     * @param parent Optional parent object that owns this instance.
     * @param Read Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwFile(SwObject* parent = nullptr)
        : SwIODevice(parent), currentMode_(Read) {}

    /**
     * @brief Constructs a `SwFile` instance.
     * @param filePath Path of the target file.
     * @param parent Optional parent object that owns this instance.
     * @param Read Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwFile(const SwString& filePath, SwObject* parent = nullptr)
        : SwIODevice(parent), currentMode_(Read) {
        filePath_ = filePath;
    }

    /**
     * @brief Destroys the `SwFile` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwFile() {
        close();
    }


    // Définir le chemin du fichier
    /**
     * @brief Sets the file Path.
     * @param filePath Path of the target file.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFilePath(const SwString& filePath) {
        filePath_ = filePath;
    }

    /**
     * @brief Returns the current file Name.
     * @return The current file Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
    /**
     * @brief Opens the underlying resource managed by the object.
     * @param mode Mode value that controls the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
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
    /**
     * @brief Opens the binary handled by the object.
     * @param mode Mode value that controls the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
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
    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() override {
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
        stopMonitoring();
    }

    // Écrire dans le fichier
    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param data Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool write(const SwString& data) {
        if (currentMode_ != Write && currentMode_ != Append) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert en mode écriture.";
        }
        fileStream_ << data;
        fileStream_.flush();
        return fileStream_.good();
    }

    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param data Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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

    /**
     * @brief Returns the current all.
     * @return The current all.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
    /**
     * @brief Returns whether the object reports open.
     * @return `true` when the object reports open; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isOpen() const override {
        return fileStream_.is_open();
    }

    /**
     * @brief Returns the current directory.
     * @return The current directory.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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


    /**
     * @brief Returns whether the object reports file.
     * @param path Path used by the operation.
     * @return The requested file.
     *
     * @details This query does not modify the object state.
     */
    static bool isFile(const SwString& path) {
        return swFilePlatform().isFile(path);
    }


    /**
     * @brief Performs the `contains` operation.
     * @param keyword Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
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

    /**
     * @brief Performs the `readLine` operation on the associated resource.
     * @param lineNumber Value passed to the method.
     * @return The resulting line.
     */
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

    /**
     * @brief Returns the current line.
     * @return The current line.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Returns the current at End.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool atEnd() const {
        if (!fileStream_.is_open()) {
            swCError(kSwLogCategory_SwFile) << "Fichier non ouvert.";
        }
        return fileStream_.eof();
    }

    /**
     * @brief Performs the `readChunk` operation on the associated resource.
     * @param chunkSize Value passed to the method.
     * @return The resulting chunk.
     */
    SwString readChunk(std::size_t chunkSize) {
        if (currentMode_ != Read) {
            throw std::runtime_error("Fichier non ouvert en mode lecture.");
        }

        std::string buffer(chunkSize, '\0');
        fileStream_.read(&buffer[0], chunkSize);
        buffer.resize(fileStream_.gcount()); // Ajuster à la taille réelle lue

        return buffer;
    }

    /**
     * @brief Performs the `seek` operation.
     * @param position Value passed to the method.
     */
    void seek(std::streampos position) {
        if (!isOpen()) {
            throw std::runtime_error("Fichier non ouvert.");
        }
        fileStream_.seekg(position);
    }

    /**
     * @brief Returns the current current Position.
     * @return The current current Position.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::streampos currentPosition() {
        if (!isOpen()) {
            throw std::runtime_error("Fichier non ouvert.");
        }
        return fileStream_.tellg();
    }


    /**
     * @brief Performs the `readLinesInRangeLazy` operation on the associated resource.
     * @param startLine Value passed to the method.
     * @param endLine Value passed to the method.
     * @return The resulting lines In Range Lazy.
     */
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

    /**
     * @brief Performs the `copy` operation.
     * @param source Value passed to the method.
     * @param destination Value passed to the method.
     * @param overwrite Value passed to the method.
     * @return The requested copy.
     */
    static bool copy(const SwString& source, const SwString& destination, bool overwrite = true) {
        return swFilePlatform().copy(source, destination, overwrite);
    }

    /**
     * @brief Performs the `copyByChunk` operation.
     * @param source Value passed to the method.
     * @param destination Value passed to the method.
     * @param nonBlocking Value passed to the method.
     * @param chunkSize Value passed to the method.
     * @return The requested copy By Chunk.
     */
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


    /**
     * @brief Performs the `copyByChunk` operation.
     * @param destination Value passed to the method.
     * @param nonBlocking Value passed to the method.
     * @param chunkSize Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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
    /**
     * @brief Performs the `getFileMetadata` operation.
     * @param creationTime Value passed to the method.
     * @param lastAccessTime Value passed to the method.
     * @param lastWriteTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool getFileMetadata(SwDateTime& creationTime, SwDateTime& lastAccessTime, SwDateTime& lastWriteTime) {
        return swFilePlatform().getFileMetadata(filePath_, creationTime, lastAccessTime, lastWriteTime);
    }

    // Modifier la date de création
    /**
     * @brief Sets the creation Time.
     * @param creationTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setCreationTime(SwDateTime creationTime) {
        return swFilePlatform().setCreationTime(filePath_, creationTime);
    }

    // Modifier la date de dernière modification
    /**
     * @brief Sets the last Write Date.
     * @param lastWriteTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setLastWriteDate(SwDateTime lastWriteTime) {
        return swFilePlatform().setLastWriteDate(filePath_, lastWriteTime);
    }

    // Modifier la date de dernier accès
    /**
     * @brief Sets the last Access Date.
     * @param lastAccessTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setLastAccessDate(SwDateTime lastAccessTime) {
        return swFilePlatform().setLastAccessDate(filePath_, lastAccessTime);
    }

    // Modifier toutes les dates (création, dernier accès, dernière modification)
    /**
     * @brief Sets the all Dates.
     * @param creationTime Value passed to the method.
     * @param lastAccessTime Value passed to the method.
     * @param lastWriteTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setAllDates(SwDateTime creationTime, SwDateTime lastAccessTime, SwDateTime lastWriteTime) {
        return swFilePlatform().setAllDates(filePath_, creationTime, lastAccessTime, lastWriteTime);
    }

    /**
     * @brief Returns the current file Checksum.
     * @return The current file Checksum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Performs the `writeMetadata` operation on the associated resource.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool writeMetadata(const SwString& key, const SwString& value) {
        return swFilePlatform().writeMetadata(filePath_, key, value);
    }

    /**
     * @brief Performs the `readMetadata` operation on the associated resource.
     * @param key Value passed to the method.
     * @return The resulting metadata.
     */
    SwString readMetadata(const SwString& key) {
        return swFilePlatform().readMetadata(filePath_, key);
    }
signals:
    DECLARE_SIGNAL(fileChanged, const SwString&)

private:
    std::fstream fileStream_;
    OpenMode currentMode_;

};
