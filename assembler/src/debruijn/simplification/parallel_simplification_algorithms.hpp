#pragma once

#include "standard_base.hpp"
#include "omni/graph_processing_algorithm.hpp"
#include "omni/basic_edge_conditions.hpp"
#include "omni/bulge_remover.hpp"
#include "omni/abstract_conjugate_graph.hpp"
#include "simplification_settings.hpp"

namespace debruijn {

namespace simplification {

template<class Graph>
class ParallelIterationHelper {
    typedef typename Graph::EdgeId EdgeId;
    typedef typename Graph::VertexId VertexId;
    typedef typename Graph::VertexIt const_vertex_iterator;

    const Graph& g_;
public:

    ParallelIterationHelper(const Graph& g) : g_(g) {

    }

    vector<const_vertex_iterator> VertexChunks(size_t chunk_cnt) const {
    	VERIFY(chunk_cnt > 0);
        //trying to split vertices into equal chunks, leftovers put into first chunk
    	vector<const_vertex_iterator> answer;
    	size_t vertex_cnt = g_.size();
    	size_t chunk_size = vertex_cnt / chunk_cnt;
        auto it = g_.begin();
        answer.push_back(it);
        for (size_t i = 0; i + chunk_cnt * chunk_size < vertex_cnt; ++i) {
            it++;
        }
        if (chunk_size > 0) {
    	    size_t i = 0;
            do {
                ++it;
    	    	if (++i % chunk_size == 0) 
    	    		answer.push_back(it);
            } while (it != g_.end());

            VERIFY(i == chunk_cnt * chunk_size);
        } else {
            VERIFY(it == g_.end());
            answer.push_back(it);
        }
        VERIFY(answer.back() == g_.end());
    	return answer;
    }

    vector<omnigraph::GraphEdgeIterator<Graph>> EdgeChunks(size_t chunk_cnt) const {
    	vector<omnigraph::GraphEdgeIterator<Graph>> answer;
    	for (const_vertex_iterator v_it: VertexChunks(chunk_cnt)) {
    		answer.push_back(omnigraph::GraphEdgeIterator<Graph>(g_, v_it));
    	}
    	return answer;
    }
};

template<class Graph>
class ParallelTipClippingFunctor {
    typedef typename Graph::EdgeId EdgeId;
    typedef typename Graph::VertexId VertexId;
    typedef boost::function<void(EdgeId)> HandlerF;
    typedef omnigraph::PairedVertexLock<VertexId> VertexLockT;

    Graph& g_;
    size_t length_bound_;
    double coverage_bound_;
    HandlerF handler_f_;

    size_t LockingIncomingCount(VertexId v) const {
        VertexLockT lock(v);
        return g_.IncomingEdgeCount(v);
    }

    size_t LockingOutgoingCount(VertexId v) const {
        VertexLockT lock(v);
        return g_.OutgoingEdgeCount(v);
    }

    bool IsIncomingTip(EdgeId e) const {
        return g_.length(e) <= length_bound_
                && math::le(g_.coverage(e), coverage_bound_)
                && LockingIncomingCount(g_.EdgeStart(e)) + LockingOutgoingCount(g_.EdgeStart(e)) == 1;
    }

    void RemoveEdge(EdgeId e) {
        //even full tip locking can't lead to deadlock
        VertexLockT lock1(g_.EdgeStart(e));
        VertexLockT lock2(g_.EdgeEnd(e));
        g_.DeleteEdge(e);
    }

public:

    ParallelTipClippingFunctor(Graph& g, size_t length_bound, 
                                double coverage_bound, HandlerF handler_f = 0)
            : g_(g),
              length_bound_(length_bound),
              coverage_bound_(coverage_bound),
              handler_f_(handler_f) {

    }

    bool operator()(VertexId v) {
        if (LockingOutgoingCount(v) == 0)
            return false;

        vector<EdgeId> tips;
        //don't need lock here after the previous check
        for (EdgeId e : g_.IncomingEdges(v)) {
            if (IsIncomingTip(e)) {
                tips.push_back(e);
            }
        }
        
        //if all of edges are tips, leave the longest one
        if (!tips.empty() && tips.size() == g_.IncomingEdgeCount(v)) {
            sort(tips.begin(), tips.end(), omnigraph::LengthComparator<Graph>(g_));
            tips.pop_back();
        }

        for (EdgeId e : tips) {
            if (handler_f_) {
                handler_f_(e);
            }
            //don't need any synchronization here!
            RemoveEdge(e);
        }
        return false;
    }

    bool ShouldFilterConjugate() const {
    	return false;
    }
};

//todo add conjugate filtration
template<class Graph, class ElementType>
class AlgorithmRunner {
    const Graph& g_;

public:

    const Graph& g() const {
        return g_;
    }

    AlgorithmRunner(Graph& g)
    : g_(g) {

    }

    template <class Algo, class It>
    void RunFromIterator(Algo& algo, It begin, It end) {
        for (auto it = begin; it != end; ++it) {
            algo(*it);
        }
    }

    template <class Algo, class ItVec>
    void RunFromChunkIterators(Algo& algo, const ItVec& chunk_iterators) {
        DEBUG("Running from " << chunk_iterators.size() - 1 << "chunks");
    	VERIFY(chunk_iterators.size() > 1);
#pragma omp parallel for schedule(guided)
    	for (size_t i = 0; i < chunk_iterators.size() - 1; ++i) {
    		RunFromIterator(algo, chunk_iterators[i], chunk_iterators[i+1]);
    	}
        DEBUG("Finished");
    }
private:
    DECL_LOGGER("AlgorithmRunner");
};

template<class Graph>
class ParallelSimpleBRFunctor {
    typedef typename Graph::EdgeId EdgeId;
    typedef typename Graph::VertexId VertexId;
    typedef omnigraph::PairedVertexLock<VertexId> VertexLockT;

    Graph& g_;
    size_t max_length_;
    double max_coverage_;
    double max_relative_coverage_;
    size_t max_delta_;
    double max_relative_delta_;
    boost::function<void(EdgeId)> handler_f_;

    bool LengthDiffCheck(size_t l1, size_t l2, size_t delta) const {
        return l1 <= l2 + delta  && l2 <= l1 + delta;
    }

    EdgeId Alternative(EdgeId e, const vector<EdgeId>& edges) const {
        size_t delta = omnigraph::CountMaxDifference(max_delta_, g_.length(e), max_relative_delta_);
        for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
            EdgeId candidate = *it;
            if (g_.EdgeEnd(candidate) == g_.EdgeEnd(e)
                    && candidate != e
                    && candidate != g_.conjugate(e)
                    && LengthDiffCheck(g_.length(candidate), g_.length(e), delta)) {
                return candidate;
            }
        }
        return EdgeId(0);
    }

    bool ProcessEdges(const vector<EdgeId>& edges) {
        for (EdgeId e : edges) {
            if (g_.length(e) <= max_length_ && math::le(g_.coverage(e), max_coverage_)) {
                EdgeId alt = Alternative(e, edges);
                if (alt != EdgeId(0)
                        && math::ge(g_.coverage(alt) * max_relative_coverage_, g_.coverage(e))) {
                    //todo is not work in multiple threads for now :)
                    //Reasons: id distribution, kmer-mapping
                    handler_f_(e);
                    g_.GlueEdges(e, alt);
                    return true;
                }
            }
        }
        return false;
    }

    vector<VertexId> MultiEdgeDestinations(VertexId v) const {
        vector<VertexId> answer;
        set<VertexId> destinations;
        for (EdgeId e : g_.OutgoingEdges(v)) {
            VertexId end = g_.EdgeEnd(e);
            if (destinations.count(end) > 0) {
                answer.push_back(end);
            }
            destinations.insert(end);
        }
        return answer;
    }

    VertexId SingleMultiEdgeDestination(VertexId v) const {
        vector<VertexId> dests = MultiEdgeDestinations(v);
        if (dests.size() == 1) {
            return dests.front();
        } else {
            return VertexId(0);
        }
    }

    void RemoveBulges(VertexId v) {
        bool flag = true;
        while (flag) {
            vector<EdgeId> edges(g_.out_begin(v), g_.out_end(v));
            if (edges.size() == 1)
                return;
            sort(edges.begin(), edges.end(), omnigraph::CoverageComparator<Graph>(g_));
            flag = ProcessEdges(edges);
        }
    }

    bool CheckVertex(VertexId v) const {
        VertexLockT lock(v);
        return MultiEdgeDestinations(v).size() == 1
                && MultiEdgeDestinations(g_.conjugate(v)).size() == 0;
    }

    size_t MinId(VertexId v) const {
        return std::min(v.int_id(), g_.conjugate(v).int_id());
    }

    bool IsMinimal(VertexId v1, VertexId v2) const {
        return MinId(v1) < MinId(v2);
    }

public:

    ParallelSimpleBRFunctor(Graph& g,
                            size_t max_length,
                            double max_coverage,
                            double max_relative_coverage,
                            size_t max_delta,
                            double max_relative_delta,
                            boost::function<void(EdgeId)> handler_f = 0)
            : g_(g),
              max_length_(max_length),
              max_coverage_(max_coverage),
              max_relative_coverage_(max_relative_coverage),
              max_delta_(max_delta),
              max_relative_delta_(max_relative_delta),
              handler_f_(handler_f) {

    }

    bool operator()(VertexId v/*, need number of vertex for stable id distribution*/) {
        vector<VertexId> multi_dest;

        {
            VertexLockT lock(v);
            multi_dest = MultiEdgeDestinations(v);
        }

        if (multi_dest.size() == 1 && IsMinimal(v, multi_dest.front())) {
            VertexId dest = multi_dest.front();
            if (CheckVertex(v) && CheckVertex(g_.conjugate(dest))) {
                VertexLockT lock1(v);
                VertexLockT lock2(dest);
                RemoveBulges(v);
            }
        }
        return false;
    }

    bool ShouldFilterConjugate() const {
    	return false;
    }
};

template<class Graph>
class ParallelLowCoverageFunctor {
    typedef typename Graph::EdgeId EdgeId;
    typedef typename Graph::VertexId VertexId;
    typedef boost::function<void(EdgeId)> HandlerF;
    typedef omnigraph::PairedVertexLock<VertexId> VertexLockT;

    Graph& g_;
    typename Graph::HelperT helper_;
    shared_ptr<func::Predicate<EdgeId>> ec_condition_;
    HandlerF handler_f_;

    vector<EdgeId> edges_to_remove_;

    void UnlinkEdgeFromStart(EdgeId e) {
        VertexId start = g_.EdgeStart(e);
        VertexLockT lock(start);
        helper_.DeleteLink(start, e);
    }

    void UnlinkEdge(EdgeId e) {
        UnlinkEdgeFromStart(e);
        if (g_.conjugate(e) != e)
            UnlinkEdgeFromStart(g_.conjugate(e));
    }

public:

    //should be launched with conjugate copies filtered
    ParallelLowCoverageFunctor(Graph& g,
                               size_t max_length,
                               double max_coverage,
                               HandlerF handler_f = 0)
            : g_(g),
              helper_(g_.GetConstructionHelper()),
              ec_condition_(
                      func::And<EdgeId>(
                      func::And<EdgeId>(make_shared<omnigraph::LengthUpperBound<Graph>>(g, max_length),
                                        make_shared<omnigraph::CoverageUpperBound<Graph>>(g, max_coverage)),
                      make_shared<omnigraph::AlternativesPresenceCondition<Graph>>(g))),
              handler_f_(handler_f)
    {

    }

    bool IsOfInterest(EdgeId e) const {
        return ec_condition_->Check(e);
    }

    void PrepareForProcessing(size_t /*interesting_cnt*/) {
    }

    //no conjugate copies here!
    bool Process(EdgeId e, size_t /*idx*/) {
        handler_f_(e);
        g_.FireDeleteEdge(e);
        UnlinkEdge(e);
        helper_.DeleteUnlinkedEdge(e);
        return true;
    }

    bool ShouldFilterConjugate() const {
    	return true;
    }
//    bool operator()(EdgeId e) {
//        if (ec_condition_->Check(e)) {
//            edges_to_remove_.push_back(e);
//        }
//        return false;
//    }
//
//    void RemoveCollectedEdges() {
//        omnigraph::SmartSetIterator<Graph, EdgeId> to_delete(g_, edges_to_remove_.begin(), edges_to_remove_.end());
//        while (!to_delete.IsEnd()) {
//            EdgeId e = *to_delete;
//            handler_f_(e);
//            g_.DeleteEdge(e);
//            ++to_delete;
//        }
//    }

};

template<class Graph>
class ParallelCompressor {
    typedef typename Graph::EdgeId EdgeId;
    typedef typename Graph::EdgeData EdgeData;
    typedef typename Graph::VertexId VertexId;
    typedef omnigraph::PairedVertexLock<VertexId> VertexLockT;

    Graph& g_;
    typename Graph::HelperT helper_;
    restricted::IdSegmentStorage segment_storage_;

    bool IsBranching(VertexId v) const {
//        VertexLockT lock(v);
        return !g_.CheckUniqueOutgoingEdge(v) || !g_.CheckUniqueIncomingEdge(v);
    }

    size_t LockingIncomingCount(VertexId v) const {
        VertexLockT lock(v);
        return g_.IncomingEdgeCount(v);
    }

    size_t LockingOutgoingCount(VertexId v) const {
        VertexLockT lock(v);
        return g_.OutgoingEdgeCount(v);
    }

    vector<VertexId> LockingNextVertices(VertexId v) const {
        VertexLockT lock(v);
        vector<VertexId> answer;
        for (EdgeId e : g_.OutgoingEdges(v)) {
            answer.push_back(g_.EdgeEnd(e));
        }
        return answer;
    }

    vector<VertexId> FilterBranchingVertices(const vector<VertexId>& vertices) const {
        vector<VertexId> answer;
        for (VertexId v : vertices) {
            VertexLockT lock(v);
            if (!IsBranching(v)) {
                answer.push_back(v);
            }
        }
        return answer;
    }

    //correctly handles self-conjugate case
    bool IsMinimal(VertexId v1, VertexId v2) const {
        return !(g_.conjugate(v2) < v1);
    }

    //true if need to go further, false if stop on any reason!
    //to_compress is not empty only if compression needs to be done
    //don't need additional checks for v == init | conjugate(init), because init is branching!
    //fixme what about plasmids?! =)
    bool ProcessNextAndGo(VertexId& v, VertexId init, vector<VertexId>& to_compress) {
        VertexLockT lock(v);
        if (!CheckConsistent(v)) {
            to_compress.clear();
            return false;
        }
        if (IsBranching(v) ) {
            if (!IsMinimal(init, v)) {
                to_compress.clear();
            }
            return false;
        } else {
            to_compress.push_back(v);
            v = g_.EdgeEnd(g_.GetUniqueOutgoingEdge(v));
            return true;
        }
    }

    void UnlinkEdge(VertexId v, EdgeId e) {
        VertexLockT lock(v);
        helper_.DeleteLink(v, e);
    }

    void UnlinkEdges(VertexId v) {
        VertexLockT lock(v);
        helper_.DeleteLink(v, g_.GetUniqueOutgoingEdge(v));
        helper_.DeleteLink(g_.conjugate(v), g_.GetUniqueOutgoingEdge(g_.conjugate(v)));
    }

    //fixme duplication with abstract conj graph
    //not locking!
    vector<EdgeId> EdgesToDelete(const vector<EdgeId> &path) const {
        set<EdgeId> edgesToDelete;
        edgesToDelete.insert(path[0]);
        for (size_t i = 0; i + 1 < path.size(); i++) {
            EdgeId e = path[i + 1];
            if (edgesToDelete.find(g_.conjugate(e)) == edgesToDelete.end())
                edgesToDelete.insert(e);
        }
        return vector<EdgeId>(edgesToDelete.begin(), edgesToDelete.end());
    }

    //not locking!
    //fixme duplication with abstract conj graph
    vector<VertexId> VerticesToDelete(
            const vector<EdgeId> &path) const {
        set<VertexId> verticesToDelete;
        for (size_t i = 0; i + 1 < path.size(); i++) {
            EdgeId e = path[i + 1];
            VertexId v = g_.EdgeStart(e);
            if (verticesToDelete.find(g_.conjugate(v)) == verticesToDelete.end())
                verticesToDelete.insert(v);
        }
        return vector<VertexId>(verticesToDelete.begin(),
                                verticesToDelete.end());
    }
    //todo end duplication with abstract conj graph

    //not locking!
    vector<EdgeId> CollectEdges(const vector<VertexId>& to_compress) const {
        vector<EdgeId> answer;
        answer.push_back(g_.GetUniqueIncomingEdge(to_compress.front()));
        for (VertexId v : to_compress) {
            answer.push_back(g_.GetUniqueOutgoingEdge(v));
        }
        return answer;
    }

    void CallHandlers(const vector<EdgeId>& edges, EdgeId new_edge) const {
        g_.FireMerge(edges, new_edge);
        g_.FireDeletePath(EdgesToDelete(edges), VerticesToDelete(edges));
        g_.FireAddEdge(new_edge);
    }

    EdgeData MergedData(const vector<EdgeId>& edges) const {
        vector<const EdgeData*> to_merge;
        for (EdgeId e : edges) {
            to_merge.push_back(&(g_.data(e)));
        }
        return g_.master().MergeData(to_merge);
    }

    EdgeId SyncAddEdge(VertexId v1, VertexId v2, const EdgeData& data, IdDistributor& id_distributor) {
        EdgeId new_edge = helper_.AddEdge(data, id_distributor);
        {
            VertexLockT lock(v1);
            helper_.LinkOutgoingEdge(v1, new_edge);
        }
        if (g_.conjugate(new_edge) != new_edge) {
            VertexLockT lock(v2);
            helper_.LinkIncomingEdge(v2, new_edge);
        }
        return new_edge;
    }

    void ProcessBranching(VertexId next, VertexId init, size_t idx) {
        vector<VertexId> to_compress;
        while (ProcessNextAndGo(next, init, to_compress)) {
        }

        if (!to_compress.empty()) {
            //here we are sure that we are the ones to process the path
            //so we can collect edges without any troubles (and actually without locks todo check!)
            vector<EdgeId> edges = CollectEdges(to_compress);

            restricted::ListIdDistributor<restricted::SegmentIterator> id_distributor =
                    segment_storage_.GetSegmentIdDistributor(2 * idx, 2 * idx + 1);

            EdgeId new_edge = SyncAddEdge(g_.EdgeStart(edges.front()), g_.EdgeEnd(edges.back()),
                                      MergeSequences(g_, edges), id_distributor);

            CallHandlers(edges, new_edge);

            VertexId final = g_.EdgeEnd(edges.back());
            UnlinkEdge(init, edges.front());
            for (VertexId v: VerticesToDelete(edges/*to_compress*/)) {
                UnlinkEdges(v);
            }
            
            if (g_.conjugate(new_edge) != new_edge) {
                UnlinkEdge(g_.conjugate(final), g_.conjugate(edges.back()));
            }

            for (EdgeId e : EdgesToDelete(edges)) {
                helper_.DeleteUnlinkedEdge(e);
            }
        }
    }
    
    //vertex is not consistent if the path has already been compressed or under compression right now
    //not needed here, but could check if vertex is fully isolated
    bool CheckConsistent(VertexId v) const {
        //todo change to incoming edge count
        return g_.OutgoingEdgeCount(g_.conjugate(v)) > 0;
    }

    //long, but safe way to get left neighbour
    //heavily relies on the current graph structure!
    VertexId LockingGetInit(VertexId v) {
        VertexLockT lock(v);
        if (!CheckConsistent(v))
            return VertexId(0);

        //works even if this edge is already unlinked from the vertex =)
        VERIFY(g_.CheckUniqueIncomingEdge(v));
        return g_.EdgeStart(g_.GetUniqueIncomingEdge(v));
    }

public:

    ParallelCompressor(Graph& g)
            : g_(g), helper_(g_.GetConstructionHelper()) {

    }

    //returns true iff v is the "leftmost" vertex to compress in the chain
    bool IsOfInterest(VertexId v) const {
        return !IsBranching(v) && IsBranching(g_.EdgeStart(g_.GetUniqueIncomingEdge(v)));
    }

    void PrepareForProcessing(size_t interesting_cnt) {
        segment_storage_ =
                g_.GetGraphIdDistributor().Reserve(interesting_cnt * 2);
    }

    bool Process(VertexId v, size_t idx) {
		VertexId init = LockingGetInit(v);
		if (init != VertexId(0))
			ProcessBranching(v, init, idx);
        return false;
    }

    bool ShouldFilterConjugate() const {
    	return false;
    }

};

template<class Graph, class ElementType>
class TwoStepAlgorithmRunner {
    typedef typename Graph::VertexId VertexId;
    typedef typename Graph::EdgeId EdgeId;

    const Graph& g_;
    const bool filter_conjugate_;
    vector<vector<ElementType>> elements_of_interest_;

    template <class Algo>
    void ProcessBucket(Algo& algo, const vector<ElementType>& bucket, size_t idx_offset) const {
    	for (ElementType el : bucket) {
    		algo.Process(el, idx_offset++);
    	}
    }

    template <class Algo>
    void Process(Algo& algo) const {
    	vector<size_t> cumulative_bucket_sizes;
    	cumulative_bucket_sizes.push_back(0);
    	for (const auto& bucket : elements_of_interest_) {
        	cumulative_bucket_sizes.push_back(cumulative_bucket_sizes.back() + bucket.size());
    	}
        DEBUG("Preparing for processing");
    	algo.PrepareForProcessing(cumulative_bucket_sizes.back());
        DEBUG("Processing buckets");
#pragma omp parallel for schedule(guided)
        for (size_t i = 0; i < elements_of_interest_.size(); ++i) {
        	ProcessBucket(algo, elements_of_interest_[i], cumulative_bucket_sizes[i]);
        }
    }

    template <class Algo>
    void CountElement(Algo& algo, ElementType el, size_t bucket) {
    	if (filter_conjugate_ && g_.conjugate(el) < el)
    		return;
        if (algo.IsOfInterest(el))
        	elements_of_interest_[bucket].push_back(el);
    }

    template <class Algo, class It>
    void CountAll(Algo& algo, It begin, It end, size_t bucket) {
        for (auto it = begin; !(it == end); ++it) {
            CountElement(algo, *it, bucket);
        }
    }

public:

    const Graph& g() const {
        return g_;
    }

    //conjugate elements are filtered based on ids
    //should be used only if both conjugate elements are simultaneously either interesting or not
    //fixme filter_conjugate is redundant
    TwoStepAlgorithmRunner(Graph& g, bool filter_conjugate)
    : g_(g), filter_conjugate_(filter_conjugate) {

    }

    template <class Algo, class ItVec>
    void RunFromChunkIterators(Algo& algo, const ItVec& chunk_iterators) {
        DEBUG("Started running from " << chunk_iterators.size() - 1 << " chunks");
    	VERIFY(algo.ShouldFilterConjugate() == filter_conjugate_);
    	VERIFY(chunk_iterators.size() > 1);
    	elements_of_interest_.clear();
    	elements_of_interest_.resize(chunk_iterators.size() - 1);
        DEBUG("Searching elements of interest");
#pragma omp parallel for schedule(guided)
    	for (size_t i = 0; i < chunk_iterators.size() - 1; ++i) {
            CountAll(algo, chunk_iterators[i], chunk_iterators[i+1], i);
    	}
        DEBUG("Processing");
    	Process(algo);
        DEBUG("Finished");
    }

    template <class Algo, class It>
    void RunFromIterator(Algo& algo, It begin, It end) {
    	RunFromChunkIterators(algo, vector<It>{begin, end});
    }
private:
    DECL_LOGGER("TwoStepAlgorithmRunner");
};

template<class Graph, class AlgoRunner, class Algo>
void RunVertexAlgorithm(Graph& g, AlgoRunner& runner, Algo& algo, size_t chunk_cnt) {
	runner.RunFromChunkIterators(algo, ParallelIterationHelper<Graph>(g).VertexChunks(chunk_cnt));
}

template<class Graph, class AlgoRunner, class Algo>
void RunEdgeAlgorithm(Graph& g, AlgoRunner& runner, Algo& algo, size_t chunk_cnt) {
	runner.RunFromChunkIterators(algo, ParallelIterationHelper<Graph>(g).EdgeChunks(chunk_cnt));
}

}

}