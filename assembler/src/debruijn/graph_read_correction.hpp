//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#pragma once

#include "standard.hpp"
#include "omni/path_processor.hpp"
#include "io/modifying_reader_wrapper.hpp"

namespace debruijn_graph {

template<class gp_t>
class TipsProjector {
	typedef typename gp_t::graph_t Graph;
	typedef typename Graph::EdgeId EdgeId;

	gp_t& gp_;

	const UniquePathFinder<Graph> unique_path_finder_;

	optional<EdgeId> UniqueAlternativeEdge(EdgeId tip, bool outgoing_tip) {
		vector<EdgeId> edges;
		if (outgoing_tip) {
			edges = gp_.g.OutgoingEdges(gp_.g.EdgeStart(tip));
		} else {
			edges = gp_.g.IncomingEdges(gp_.g.EdgeEnd(tip));
		}
		restricted::set<EdgeId> edges_set(edges.begin(), edges.end());
		edges_set.erase(tip);
		if (edges_set.size() == 1)
			return optional < EdgeId > (*edges_set.begin());
		else
			return boost::none;
	}

	vector<EdgeId> UniqueAlternativePath(EdgeId tip, bool outgoing_tip) {
		optional<EdgeId> alt_edge = UniqueAlternativeEdge(tip, outgoing_tip);
		if (alt_edge) {
			if (outgoing_tip) {
				return unique_path_finder_.UniquePathForward(*alt_edge);
			} else {
				return unique_path_finder_.UniquePathBackward(*alt_edge);
			}
		}
		return vector<EdgeId>();
	}

	void AlignAndProject(const Sequence& tip_seq, const Sequence& alt_seq,
			bool outgoing_tip) {
		//todo refactor
		Sequence aligned_tip = tip_seq;
		Sequence aligned_alt = alt_seq;
		if (outgoing_tip) {
			if (tip_seq.size() >= alt_seq.size()) {
				aligned_tip = tip_seq.Subseq(0, alt_seq.size());
			} else {
				aligned_alt = alt_seq.Subseq(0, tip_seq.size());
			}
		} else {
			if (tip_seq.size() >= alt_seq.size()) {
				aligned_tip = tip_seq.Subseq(tip_seq.size() - alt_seq.size());
			} else {
				aligned_alt = alt_seq.Subseq(alt_seq.size() - tip_seq.size());
			}
		}

		INFO(
				"Remapping " << aligned_tip.size()
						<< " kmers of aligned_tip to aligned_alt");
		gp_.kmer_mapper.RemapKmers(aligned_tip, aligned_alt);
	}

	void AlignAndProject(
			const AbstractConjugateGraph<typename Graph::DataMaster>& /*graph*/,
			const Sequence& tip_seq, const Sequence& alt_seq,
			bool outgoing_tip) {
		AlignAndProject(tip_seq, alt_seq, outgoing_tip);
		AlignAndProject(!tip_seq, !alt_seq, !outgoing_tip);
	}

	void AlignAndProject(
			const AbstractNonconjugateGraph<typename Graph::DataMaster>& graph,
			const Sequence& tip_seq, const Sequence& alt_seq,
			bool outgoing_tip) {
		AlignAndProject(tip_seq, alt_seq, outgoing_tip);
	}

public:
	TipsProjector(gp_t& gp) :
			gp_(gp), unique_path_finder_(gp.g) {

	}

	void ProjectTip(EdgeId tip) {
		TRACE("Trying to project tip " << gp_.g.str(tip));
		bool outgoing_tip = gp_.g.IsDeadEnd(gp_.g.EdgeEnd(tip));
		Sequence tip_seq = gp_.g.EdgeNucls(tip);
		vector<EdgeId> alt_path = UniqueAlternativePath(tip, outgoing_tip);
		if (alt_path.empty()) {
			TRACE(
					"Failed to find unique alt path for tip " << gp_.g.str(tip)
							<< ". Wasn't projected!!!");
		} else {
			Sequence alt_seq = MergeSequences(gp_.g, alt_path);
			if (tip_seq.size() > alt_seq.size()) {
				TRACE(
						"Can't fully project tip " << gp_.g.str(tip)
								<< " with seq length " << tip_seq.size()
								<< " because alt path length is "
								<< alt_seq.size()
								<< ". Trying to project partially");
			}
			AlignAndProject(gp_.g, tip_seq, alt_seq, outgoing_tip);
			TRACE("Tip projected");
		}
	}
private:
	DECL_LOGGER("TipsProjector")
	;
};


//todo improve logging
template<class Graph, class Mapper>
class GraphReadCorrector: public io::SequenceModifier {
	typedef typename Graph::EdgeId EdgeId;
	typedef typename Graph::VertexId VertexId;

	const Graph& graph_;
	const Mapper mapper_;

	Path<EdgeId> TryFixPath(const Path<EdgeId>& path) const {
		return Path <EdgeId> (path_fixer_.TryFixPath(path.sequence()), path.start_pos(), path.end_pos());
	}

public:
	/*virtual*/
	Sequence Modify(const Sequence& s) {
//		if(s < !s)
//			return !Refine(!s);
		MappingPath<EdgeId> mapping_path = mapper_.MapSequence(s);

		if (mapping_path.size() == 0 || s.size() < graph_.k() + 1
				|| mapping_path.front().second.initial_range.start_pos != 0
				|| mapping_path.back().second.initial_range.end_pos
						!= s.size() - graph_.k()) {
			//todo reduce concat unmapped beginning and end in future???
			TRACE(
					"Won't fix because wasn't mapped or start/end fell on unprojected tip/erroneous connection");
//			TRACE(
//					"For sequence of length " << s.size()
//							<< " returning empty sequence");
			return s;
//			return Sequence();
		}

		Path<EdgeId> path = TryFixPath(mapping_path.simple_path());
//		TRACE("Mapped sequence to path " << graph_.str(path.sequence()));

		if (!path_fixer_.CheckContiguous(path.sequence())) {
			TRACE("Even fixed path wasn't contiguous");
			return s;
		} else {
			TRACE("Fixed path is contiguous");
			Sequence path_sequence = MergeSequences(graph_, path.sequence());
			size_t start = path.start_pos();
			size_t end = path_sequence.size()
					- graph_.length(path[path.size() - 1]) + path.end_pos();
			//todo we can do it more accurately with usage of mapping_path
			Sequence answer = path_sequence.Subseq(start, end);
//			if (answer != s) {
//				if (answer.size() < 1000) {
//					TRACE(
//							"Initial sequence modified, edit distance= "
//									<< EditDistance(answer, s));
//				} else {
//					TRACE("Sequence too large, won't count edit distance");
//				}
//			}
			return answer;
		}

//		else {
//			TRACE("Initial sequence unmodified!");
//		}
	}

	GraphReadCorrector(const Graph& graph, const Mapper& mapper) :
			graph_(graph), mapper_(mapper), path_fixer_(graph) {
	}

private:
	DECL_LOGGER("ContigRefiner");
	const MappingPathFixer<Graph> path_fixer_;
};

template<class Graph, class Mapper>
shared_ptr<GraphReadCorrector<Graph, Mapper>> GraphReadCorrectorInstance(
		const Graph& graph, const Mapper& mapper) {
	return std::make_shared<GraphReadCorrector<Graph, Mapper>>(graph, mapper);
}

}
