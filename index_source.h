#pragma once
#include "codecs.h"
#include <switch.h>
#include <switch_dictionary.h>

namespace Trinity
{
        // An index source provides term_index_ctx and decoders to the query execution runtime
        // It can be a RO wrapper to an index segment, a wrapper to a simple hashtable/list, anything
        // Lucene implements near real-time search by providing a segment wrapper(i.e index source) which accesses the indexer state directly
        // With index sources, we could accomplish that as well
        //
        // One could build a fairly impressive scheme, where custom IndexSource and Trinity::Codec::Decoder sub-classes would allow for very interesting use cases
        class IndexSource
            : public RefCounted<IndexSource>
        {
              protected:
                Switch::unordered_map<strwlen8_t, uint32_t> termIDsMap;
                Switch::unordered_map<uint32_t, term_index_ctx> cache;
                uint64_t gen{0}; // See IndexSourcesCollection

              public:
                inline auto generation() const noexcept
                {
                        return gen;
                }

                // Returns an INDEX SOURCE WORD SPACE integer identifier
                // You will need to translate to this words space. See exec.cpp
                // TODO: serialize access
                uint32_t resolve_term(const strwlen8_t term)
                {
                        uint32_t *p;

                        SLog("Resolving [", term, "]\n");

                        if (termIDsMap.Add(term, 0, &p))
                        {
                                if (auto tctx = resolve_term_ctx(term); 0 == tctx.documents)
                                {
                                        // Undefined in this segment
                                        SLog("UNDEFINED [", term, "]\n");
                                        *p = 0;
                                }
                                else
                                {
                                        SLog("FOR [", term, "] ", tctx.documents, "\n");
                                        *p = termIDsMap.size();
                                        cache.insert({*p, tctx});
                                }
                        }

                        SLog("For [", term, "] ", *p, "\n");
                        return *p;
                }

                // TODO: serialize access
                term_index_ctx term_ctx(const uint32_t termID /* segment space */)
                {
                        return cache[termID];
                }

                // Subclasses only need to implement 3 methods
                virtual term_index_ctx resolve_term_ctx(const strwlen8_t term) = 0;

                // factory method
                virtual Trinity::Codecs::Decoder *new_postings_decoder(const term_index_ctx ctx) = 0;

                // Override if you have any masked documents
                virtual updated_documents masked_documents()
                {
                        return {};
                }

                virtual ~IndexSource()
                {
                }
        };

        // A collection of IndexSource; an index of segments or other sources
        // Each index source is identified by a generation, and no two sources can share the same generation
        // The generation represents the order of the sources in relation to each other; a higher generation means that
        // a source with that generation has been created after another with a lower generation.
        // In practice those are likely Timings::Microseconds::SysTime() at the time of the source creation and
        // for segments thats when they were persisted to disk. (in fact for segments, their name is their
        // generation)
        //
        // Each source is also associated with an `updated_documents` instance which tracked all documents updated or deleted when the source was created.
        // This is not used directly by the source itself, but when executing a query on a source(see exec_query() API), we need to consider the source's generation
        // and for any other sources that will be involved in the search session that have generation HIGHER than the source's generation, we need to check that
        // a document is not set in any of their `updated_documents` instances (because that would mean that there is more recent information about that document
        // in another source that will be considered in this search session).
        //
        // IndexSourcesCollection facilitates that arrangement.
        // It represents a `search session` collection of index sources, and for each such source, it creates a dids_scanner_registry that contains scanners
        // for all more recent sources.
        //
        // It also retains all sources.
        // In this example, we are executing the query in sequence, but you could do this in parallel using multiple threads and just collect the top-K results from each
        // and merge/reduce them in the end.
        /*
	 *
		IndexSourcesCollection bpIndex;

		bpIndex.insert(source1);
		bpIndex.insert(source2);
		bpIndex.commit();

		source1->Release();
		source2->Release();

		for (uint32_t i{0}; i != bpIndex.sources.size(); ++i)
		{
			auto source = bpIndex.sources[i];
			auto scanner = bpIndex.scanner_registry_for(i);

			exec_query(query, source, scanner);
			free(scanner);
		}
	*
	*/
        class IndexSourcesCollection final
        {
              private:
                std::vector<updated_documents> all;
                // for each source, we track how many of the first update_documents in all[]
                // we should consider for masking documents
                std::vector<std::pair<IndexSource *, uint16_t>> map;

              public:
                std::vector<IndexSource *> sources;

              public:
                void insert(IndexSource *is)
                {
                        is->Retain();
                        sources.push_back(is);
                }

                ~IndexSourcesCollection();

                void commit();

		// remember to std::free() the result
                Trinity::dids_scanner_registry *scanner_registry_for(const uint16_t idx) ;
        };
}
