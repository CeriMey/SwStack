#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwCheckBox.h"
#include "SwComboBox.h"
#include "SwSlider.h"
#include "SwVideoWidget.h"
#include "SwObject.h"

#include "media/SwAudioDecoder.h"
#include "media/SwMediaOpenOptions.h"
#include "media/SwMediaPlayer.h"
#include "media/SwMediaSourceFactory.h"
#include "core/types/SwMap.h"

#include <memory>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    auto printAvailableDecoders = []() {
        SwMap<SwString, SwVideoDecoderDescriptor> decoderMap;
        auto appendCodec = [&decoderMap](SwVideoPacket::Codec codec) {
            auto decoders = SwVideoWidget::availableVideoDecoders(codec);
            for (const auto& decoder : decoders) {
                if (!decoderMap.contains(decoder.id)) {
                    decoderMap.insert(decoder.id, decoder);
                }
            }
        };
        appendCodec(SwVideoPacket::Codec::H264);
        appendCodec(SwVideoPacket::Codec::H265);
        appendCodec(SwVideoPacket::Codec::AV1);
        std::cout << "[RtspVideoWidget] Available video decoders:" << std::endl;
        std::cout << "  - id=auto name=Automatic priority=0 shareable=0" << std::endl;
        for (auto it = decoderMap.begin(); it != decoderMap.end(); ++it) {
            const auto& decoder = it.value();
            std::cout << "  - id=" << decoder.id
                      << " name=" << decoder.displayName
                      << " priority=" << decoder.priority
                      << " shareable=" << (decoder.shareable ? 1 : 0)
                      << std::endl;
        }
        auto printAudioCodec = [](SwAudioPacket::Codec codec, const char* codecName) {
            auto decoders = SwMediaPlayer::availableAudioDecoders(codec);
            for (const auto& decoder : decoders) {
                std::cout << "  - audio codec=" << codecName
                          << " id=" << decoder.id
                          << " name=" << decoder.displayName
                          << " priority=" << decoder.priority
                          << std::endl;
            }
        };
        printAudioCodec(SwAudioPacket::Codec::PCMU, "pcmu");
        printAudioCodec(SwAudioPacket::Codec::PCMA, "pcma");
        printAudioCodec(SwAudioPacket::Codec::Opus, "opus");
        printAudioCodec(SwAudioPacket::Codec::AAC, "aac");
    };

    std::string url = "rtsp://172.16.40.80:5004/video?audio=1&metadata=1";
    std::string localBind;
    std::string decoderId;
    uint16_t rtpPort = 0;
    uint16_t rtcpPort = 0;
    if (argc > 1 && argv[1]) {
        url = argv[1];
    }
    if (argc > 2 && argv[2]) {
        localBind = argv[2];
    }
    if (argc > 3 && argv[3]) {
        rtpPort = static_cast<uint16_t>(std::atoi(argv[3]));
        rtcpPort = static_cast<uint16_t>(rtpPort + 1);
    }
    if (argc > 4 && argv[4]) {
        decoderId = argv[4];
    }
    if (decoderId == "--list-decoders") {
        printAvailableDecoders();
        return 0;
    }

    SwGuiApplication app;
    SwMainWindow window(L"SwCore - Video Player", 960, 640);

    auto* info = new SwLabel(&window);
    info->setText("Opening media source...");
    info->setMinimumSize(480, 28);

    auto* metadataInfo = new SwLabel(&window);
    metadataInfo->setText("KLV: disabled");
    metadataInfo->setMinimumSize(480, 28);

    auto* decoderRow = new SwWidget(&window);
    decoderRow->setMinimumSize(0, 44);
    auto* decoderLayout = new SwHorizontalLayout(decoderRow);
    decoderLayout->setMargin(0);
    decoderLayout->setSpacing(8);
    decoderRow->setLayout(decoderLayout);

    auto* decoderLabel = new SwLabel(decoderRow);
    decoderLabel->setText("Decoder");
    decoderLabel->setMinimumSize(84, 32);

    auto* decoderCombo = new SwComboBox(decoderRow);
    decoderCombo->setMinimumSize(260, 36);
    decoderCombo->addItem("Automatic");

    SwMap<SwString, SwVideoDecoderDescriptor> availableDecoders;
    auto appendCodec = [&availableDecoders](SwVideoPacket::Codec codec) {
        auto decoders = SwVideoWidget::availableVideoDecoders(codec);
        for (const auto& decoder : decoders) {
            if (!availableDecoders.contains(decoder.id)) {
                availableDecoders.insert(decoder.id, decoder);
            }
        }
    };
    appendCodec(SwVideoPacket::Codec::H264);
    appendCodec(SwVideoPacket::Codec::H265);
    appendCodec(SwVideoPacket::Codec::AV1);
    SwList<SwString> decoderIds;
    decoderIds.append("auto");
    for (auto it = availableDecoders.begin(); it != availableDecoders.end(); ++it) {
        const auto& decoder = it.value();
        SwString label = decoder.displayName;
        if (!decoder.id.isEmpty() && decoder.id != decoder.displayName) {
            label += SwString(" (") + decoder.id + SwString(")");
        }
        decoderCombo->addItem(label);
        decoderIds.append(decoder.id);
    }

    decoderLayout->addWidget(decoderLabel, 0, 84);
    decoderLayout->addWidget(decoderCombo, 0, 260);
    decoderLayout->addStretch(1);

    auto* audioRow = new SwWidget(&window);
    audioRow->setMinimumSize(0, 44);
    auto* audioLayout = new SwHorizontalLayout(audioRow);
    audioLayout->setMargin(0);
    audioLayout->setSpacing(8);
    audioRow->setLayout(audioLayout);

    auto* audioLabel = new SwLabel(audioRow);
    audioLabel->setText("Audio");
    audioLabel->setMinimumSize(84, 32);

    auto* audioCombo = new SwComboBox(audioRow);
    audioCombo->setMinimumSize(260, 36);
    audioCombo->addItem("Disabled");

    auto* muteCheck = new SwCheckBox("Mute", audioRow);
    muteCheck->setMinimumSize(80, 32);

    auto* volumeLabel = new SwLabel(audioRow);
    volumeLabel->setText("Volume");
    volumeLabel->setMinimumSize(80, 32);

    auto* volumeSlider = new SwSlider(SwSlider::Orientation::Horizontal, audioRow);
    volumeSlider->setMinimumSize(180, 40);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);

    audioLayout->addWidget(audioLabel, 0, 84);
    audioLayout->addWidget(audioCombo, 0, 260);
    audioLayout->addWidget(muteCheck, 0, 80);
    audioLayout->addWidget(volumeLabel, 0, 80);
    audioLayout->addWidget(volumeSlider, 0, 180);
    audioLayout->addStretch(1);

    auto* video = new SwVideoWidget(&window);
    video->setScalingMode(SwVideoWidget::ScalingMode::Fit);
    video->setBackgroundColor({8, 8, 8});

    auto player = std::make_shared<SwMediaPlayer>();
    video->setVideoSink(player->videoSink());

    auto* layout = new SwVerticalLayout(&window);
    layout->setMargin(20);
    layout->setSpacing(10);
    window.setLayout(layout);
    layout->addWidget(decoderRow, 0, 44);
    layout->addWidget(audioRow, 0, 44);
    layout->addWidget(info, 0, info->minimumSizeHint().height);
    layout->addWidget(metadataInfo, 0, metadataInfo->minimumSizeHint().height);
    layout->addWidget(video, 1);

    SwMediaOpenOptions openOptions = SwMediaOpenOptions::fromUrl(SwString(url.c_str()));
    if (!localBind.empty()) {
        openOptions.bindAddress = SwString(localBind.c_str());
    }
    if (rtpPort != 0) {
        openOptions.rtpPort = rtpPort;
        openOptions.rtcpPort = rtcpPort;
    }
    auto source = SwMediaSourceFactory::createMediaSource(openOptions);
    if (!source) {
        std::cerr << "[RtspVideoWidget] Unsupported media URL: " << url << std::endl;
        return 2;
    }
    player->setSource(source);
    player->setAudioEnabled(openOptions.enableAudio);
    player->setMetadataEnabled(openOptions.enableMetadata);
    auto applyDecoderSelection = [video](const SwString& selectedId) -> bool {
        const SwVideoPacket::Codec codecs[] = {
            SwVideoPacket::Codec::H264,
            SwVideoPacket::Codec::H265,
            SwVideoPacket::Codec::AV1
        };
        if (selectedId.isEmpty() || selectedId == "auto") {
            for (auto codec : codecs) {
                video->clearPreferredVideoDecoder(codec);
            }
            return true;
        }
        bool applied = false;
        for (auto codec : codecs) {
            if (SwVideoDecoderFactory::instance().contains(codec, selectedId)) {
                if (!video->setPreferredVideoDecoder(codec, selectedId)) {
                    return false;
                }
                applied = true;
            } else {
                video->clearPreferredVideoDecoder(codec);
            }
        }
        return applied;
    };
    auto audioTrackIds = std::make_shared<SwList<SwString>>();
    auto updateAudioTracks = [player, audioCombo, audioTrackIds]() {
        audioCombo->clear();
        audioCombo->addItem("Disabled");
        audioTrackIds->clear();
        audioTrackIds->append(SwString());
        int selectedIndex = 0;
        const auto tracks = player->tracks();
        for (const auto& track : tracks) {
            if (!track.isAudio() || !track.selected) {
                continue;
            }
            SwString label = track.codec.isEmpty() ? SwString("audio") : track.codec.toUpper();
            label += SwString(" (") + track.id + SwString(")");
            audioCombo->addItem(label);
            audioTrackIds->append(track.id);
            if (track.id == player->activeAudioTrack()) {
                selectedIndex = static_cast<int>(audioTrackIds->size()) - 1;
            }
        }
        audioCombo->setCurrentIndex(selectedIndex);
    };
    if (decoderId == "auto") {
        decoderId.clear();
    }
    if (!decoderId.empty()) {
        if (!applyDecoderSelection(SwString(decoderId.c_str()))) {
            std::cerr << "[RtspVideoWidget] Unknown decoder id: " << decoderId << std::endl;
            printAvailableDecoders();
            return 2;
        }
    }

    int selectedDecoderIndex = 0;
    for (std::size_t i = 0; i < decoderIds.size(); ++i) {
        if (decoderIds[i] == SwString(decoderId.c_str())) {
            selectedDecoderIndex = static_cast<int>(i);
            break;
        }
    }
    decoderCombo->setCurrentIndex(selectedDecoderIndex);

    auto updateInfoLabel = [info, &url, player](const SwString& activeDecoderId) {
        SwString infoText = SwString("Streaming from: ") + SwString(url.c_str());
        infoText += SwString(" | decoder: ");
        infoText += activeDecoderId.isEmpty() ? SwString("auto") : activeDecoderId;
        if (!player->activeAudioTrack().isEmpty()) {
            infoText += SwString(" | audio: ") + player->activeAudioTrack();
        } else if (!player->isAudioEnabled()) {
            infoText += SwString(" | audio: disabled");
        }
        if (!player->activeMetadataTrack().isEmpty()) {
            infoText += SwString(" | klv: ") + player->activeMetadataTrack();
        } else if (!player->isMetadataEnabled()) {
            infoText += SwString(" | klv: disabled");
        }
        info->setText(infoText);
    };

    auto updateMetadataLabel = [metadataInfo, player]() {
        if (!player->isMetadataEnabled()) {
            metadataInfo->setText("KLV: disabled");
            return;
        }
        if (player->activeMetadataTrack().isEmpty()) {
            metadataInfo->setText("KLV: waiting track...");
            return;
        }
        metadataInfo->setText(SwString("KLV: waiting packets on ") + player->activeMetadataTrack());
    };

    player->setTracksChangedCallback([updateAudioTracks, updateInfoLabel, updateMetadataLabel, decoderId](const SwList<SwMediaTrack>&) {
        updateAudioTracks();
        updateInfoLabel(SwString(decoderId.c_str()));
        updateMetadataLabel();
    });

    player->setMetadataPacketCallback([metadataInfo](const SwMediaPacket& packet) {
        SwString text = "KLV";
        if (!packet.codec().isEmpty()) {
            text += SwString(" ") + packet.codec().toUpper();
        }
        if (!packet.trackId().isEmpty()) {
            text += SwString(" ") + packet.trackId();
        }
        text += SwString(" bytes=") + SwString(std::to_string(packet.payload().size()));
        text += SwString(" pts=") + SwString(std::to_string(packet.pts()));
        metadataInfo->setText(text);
    });

    updateInfoLabel(SwString(decoderId.c_str()));
    updateAudioTracks();
    updateMetadataLabel();

    SwObject::connect(decoderCombo, &SwComboBox::currentIndexChanged, decoderRow, [video,
                                                                                   decoderCombo,
                                                                                   decoderIds,
                                                                                   applyDecoderSelection,
                                                                                   updateInfoLabel](int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= decoderIds.size()) {
            return;
        }
        const SwString selectedId = decoderIds[static_cast<std::size_t>(index)];
        if (selectedId == "auto") {
            applyDecoderSelection(SwString());
            updateInfoLabel(SwString());
            return;
        }
        if (!applyDecoderSelection(selectedId)) {
            swCWarning("sw.exemples.rtspvideowidget")
                << "[RtspVideoWidget] Failed to switch decoder to id=" << selectedId;
            decoderCombo->setCurrentIndex(0);
            applyDecoderSelection(SwString());
            updateInfoLabel(SwString());
            return;
        }
        updateInfoLabel(selectedId);
    });

    SwObject::connect(audioCombo, &SwComboBox::currentIndexChanged, audioRow, [player,
                                                                               audioTrackIds,
                                                                               updateInfoLabel,
                                                                               decoderId](int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= audioTrackIds->size()) {
            return;
        }
        const SwString trackId = (*audioTrackIds)[static_cast<std::size_t>(index)];
        player->setAudioEnabled(!trackId.isEmpty());
        player->setActiveAudioTrack(trackId);
        updateInfoLabel(SwString(decoderId.c_str()));
    });

    SwObject::connect(muteCheck, &SwCheckBox::toggled, audioRow, [player](bool checked) {
        player->setMuted(checked);
    });

    SwObject::connect(volumeSlider, &SwSlider::valueChanged, audioRow, [player](int value) {
        player->setVolume(static_cast<float>(value) / 100.0f);
    });

    player->play();
    window.show();

    int code = app.exec();
    player->stop();
    return code;
}
