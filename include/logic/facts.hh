#ifndef _LOGIC_FACTS_HH_
#define _LOGIC_FACTS_HH_

#include <logic/expression.hh>

#include <boost/logic/tribool.hpp>

#include <iostream>
#include <set>
#include <vector>

namespace hyper {
	namespace logic {
		class facts {
			public:
				typedef std::set<function_call> expressionS;
				typedef std::vector<expressionS> factsV;
				typedef expressionS::const_iterator const_iterator;

				/* A list of sub-expression which appears for each category */
				typedef std::set<expression> sub_expressionS;
				typedef std::vector<sub_expressionS> sub_expressionV;
				typedef sub_expressionS::const_iterator sub_const_iterator;

			private:
				const funcDefList& funcs;
				mutable factsV list;
				mutable sub_expressionV sub_list;
				size_t size__;

			public:	
				facts(const funcDefList& funcs_): funcs(funcs_), size__(0) {}

				bool add(const std::string& s);
				bool add(const function_call& f);

				boost::logic::tribool matches(const function_call & e) const;

				const_iterator begin(functionId id) const { 
					if (id >= list.size())
						list.resize(funcs.size());
					return list[id].begin();
				}

				const_iterator end(functionId id) const { 
					if (id >= list.size())
						list.resize(funcs.size());
					return list[id].end();
				}

				sub_const_iterator sub_begin(functionId id) const {
					if (id >= sub_list.size())
						sub_list.resize(funcs.size());
					return sub_list[id].begin();
				}

				sub_const_iterator sub_end(functionId id) const {
					if (id >= sub_list.size())
						sub_list.resize(funcs.size());
					return sub_list[id].end();
				}

				size_t size(functionId id) const {
					if (id >= list.size())
						list.resize(funcs.size());
					return list[id].size();
				}

				size_t size() const { return size__; }

				friend std::ostream& operator << (std::ostream& os, const facts&);
		};

		std::ostream& operator << (std::ostream& os, const facts&);
	}
}

#endif /* _LOGIC_FACTS_HH_ */