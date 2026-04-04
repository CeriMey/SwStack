namespace swEmbeddedDbDetail {

struct ReadPrimaryRow_ {
    SwByteArray primaryKey;
    PrimaryRecord_ record;
};

struct ReadPrimarySegment_ {
    SwByteArray firstKey;
    SwByteArray lastKey;
    SwList<ReadPrimaryRow_> rows;
    unsigned long long approximateBytes{0};
};

struct ReadIndexRow_ {
    SwByteArray compositeKey;
    SwByteArray secondaryKey;
    unsigned int primarySegmentIndex{0};
    unsigned int primaryRowIndex{0};
};

struct ReadIndexSegment_ {
    SwByteArray firstCompositeKey;
    SwByteArray lastCompositeKey;
    SwList<ReadIndexRow_> rows;
    unsigned long long approximateBytes{0};
};

class ReadModel_ {
public:
    unsigned long long visibleSequence{0};
    unsigned long long approximateBytes{0};
    SwList<ReadPrimarySegment_> primarySegments;
    SwHash<SwString, SwList<ReadIndexSegment_> > indexSegments;

    static std::size_t rowsPerSegment() {
        return 1024u;
    }
};

inline unsigned long long estimateSecondaryKeysBytes_(
    const SwMap<SwString, SwList<SwByteArray> >& secondaryKeys) {
    unsigned long long bytes = 0;
    for (SwMap<SwString, SwList<SwByteArray> >::const_iterator it = secondaryKeys.begin();
         it != secondaryKeys.end();
         ++it) {
        bytes += static_cast<unsigned long long>(it.key().size());
        for (std::size_t i = 0; i < it.value().size(); ++i) {
            bytes += static_cast<unsigned long long>(it.value()[i].size());
        }
    }
    return bytes;
}

inline unsigned long long estimateEntryBytes_(const SwDbEntry& entry) {
    return static_cast<unsigned long long>(entry.primaryKey.size() +
                                           entry.secondaryKey.size() +
                                           entry.value.size()) +
           estimateSecondaryKeysBytes_(entry.secondaryKeys) +
           96ull;
}

inline unsigned long long estimatePrimaryRowBytes_(const ReadPrimaryRow_& row) {
    return static_cast<unsigned long long>(row.primaryKey.size() + row.record.value.size()) +
           estimateSecondaryKeysBytes_(row.record.secondaryKeys) +
           96ull;
}

inline unsigned long long estimateIndexRowBytes_(const ReadIndexRow_& row) {
    return static_cast<unsigned long long>(row.compositeKey.size() + row.secondaryKey.size()) + 40ull;
}

inline void finalizePrimarySegment_(ReadPrimarySegment_& segment, ReadModel_& readModel) {
    if (segment.rows.isEmpty()) {
        return;
    }
    segment.firstKey = segment.rows.first().primaryKey;
    segment.lastKey = segment.rows.last().primaryKey;
    readModel.approximateBytes += segment.approximateBytes;
    readModel.primarySegments.append(std::move(segment));
    segment = ReadPrimarySegment_();
}

inline void finalizeIndexSegment_(const SwString& indexName,
                                  ReadIndexSegment_& segment,
                                  ReadModel_& readModel) {
    if (segment.rows.isEmpty()) {
        return;
    }
    segment.firstCompositeKey = segment.rows.first().compositeKey;
    segment.lastCompositeKey = segment.rows.last().compositeKey;
    readModel.approximateBytes += segment.approximateBytes;
    readModel.indexSegments[indexName].append(std::move(segment));
    segment = ReadIndexSegment_();
}

} // namespace swEmbeddedDbDetail
