#pragma once
#pragma warning(push)
#pragma warning(disable: 4505)

struct StringFilter
{
	enum class Type
	{
		kNone = -1,
		kActorKeyword,
		kArmorKeyword,
		kMagicKeyword,
		kFormEditorID,

		kTotal
	};

	std::string						str;
	Type							type = Type::kFormEditorID;
	bool							isNegate = false;
};

class StringFilterList
{
	enum class Flag
	{
		kActorKeyword = 1 << (uint32_t)StringFilter::Type::kActorKeyword,
		kArmorKeyword = 1 << (uint32_t)StringFilter::Type::kArmorKeyword,
		kMagicKeyword = 1 << (uint32_t)StringFilter::Type::kMagicKeyword,
		kFormEditorID = 1 << (uint32_t)StringFilter::Type::kFormEditorID,
	};

	std::vector<StringFilter>		data;
	stl::enumeration<Flag,uint32_t>	flags;

public:
	void Add( StringFilter a_filter )
	{
		data.push_back( a_filter );
		flags.set( (Flag)( 1 << (uint32_t)a_filter.type ) );
	}

	bool HasFilterType( StringFilter::Type a_type )
	{
		return flags.any( (Flag)( 1 << (uint32_t)a_type ) );
	}

	bool FormHasKeywords( RE::BGSKeywordForm* a_form, StringFilter::Type a_type = StringFilter::Type::kNone )
	{
		for( auto& keyword : data )
		{
			bool hasKeyword = a_form->HasKeywordString( keyword.str );

			if( keyword.isNegate )
				hasKeyword = !hasKeyword;

			if( !hasKeyword && (keyword.type == a_type || a_type == StringFilter::Type::kNone ) )
				return false;
		}

		return true;
	}

	bool FormEditorIDMatch( RE::TESForm* a_form )
	{
		for( auto& keyword : data )
		{
			if( keyword.type == StringFilter::Type::kFormEditorID )
			{
				std::regex filter( keyword.str );
				if( !std::regex_match( a_form->GetFormEditorID(), filter ) )
					return false;
			}
		}
		
		return true;
	}

	bool ActiveEffectsHasKeywords( RE::Actor* a_actor )
	{
		if( !HasFilterType( StringFilter::Type::kMagicKeyword ) )
			return true;

		// Search active effects if both actor and armor has none of the keyword
		auto activeEffects = a_actor->GetActiveEffectList();
		for( auto activeEffect : *activeEffects )
		{
			// Effect must active to count for keyword matching
			if( activeEffect->flags.none( RE::ActiveEffect::Flag::kInactive ) &&
				FormHasKeywords( activeEffect->effect->baseEffect, StringFilter::Type::kMagicKeyword ) )
				return true;
		}

		return false;
	}

	bool ActorHasKeywords( RE::Actor* a_actor )
	{
		if( !HasFilterType( StringFilter::Type::kActorKeyword ) )
			return true;

		for( auto& keyword : data )
		{
			bool hasKeyword = a_actor->HasKeywordString( keyword.str );
			if( keyword.isNegate )
				hasKeyword = !hasKeyword;

			if( !hasKeyword && keyword.type == StringFilter::Type::kActorKeyword )
				return false;
		}

		return true;
	}

	bool ArmorHasKeywords( RE::Actor* a_actor )
	{
		if( !HasFilterType( StringFilter::Type::kArmorKeyword ) )
			return true;

		const auto inv = a_actor->GetInventory([](RE::TESBoundObject& a_object) {
			return a_object.IsArmor();
			});

		for( const auto& [item, invData] : inv ) 
		{
			const auto& [count, entry] = invData;
			if( count > 0 && entry->IsWorn() ) 
			{
				const auto armor = item->As<RE::TESObjectARMO>();
				if( armor && FormHasKeywords( armor, StringFilter::Type::kArmorKeyword ) ) 
					return true;
			}
		}

		return false;
	}

	bool Evaluate( RE::TESForm* a_form )
	{
		if( a_form->Is( RE::FormType::ActorCharacter ) )
		{
			auto actor = static_cast<RE::Actor*>( a_form );
			bool isActorHasKeyword = ActorHasKeywords( actor );
			bool isArmorHasKeyword = ArmorHasKeywords( actor );
			bool isMagicHasKeyword = ActiveEffectsHasKeywords( actor );

			return isActorHasKeyword && isArmorHasKeyword && isMagicHasKeyword;
		}
		else if( a_form->Is( RE::FormType::Race ) )
		{
			return FormEditorIDMatch( a_form );
		}

		return false;
	}
};

static std::vector<std::string> split( const char* a_input, const char* a_regex ) 
{
	// passing -1 as the submatch index parameter performs splitting
	std::string in(a_input);
	std::regex re(a_regex);
	std::sregex_token_iterator
		first{in.begin(), in.end(), re, -1},
		last;
	return {first, last};
}

extern std::regex g_sExcludeRegexp;
extern std::regex g_PlayerNodes;
static RE::NiNode* FindClosestHitNode( RE::NiNode* a_root, RE::NiPoint3* a_pos, float& a_dist, bool a_isPlayer )
{
	float childMinDist = 1000000;
	RE::NiNode* childNode = NULL;
	if( a_root->children.size() > 0 )
	{
		for( auto iter = a_root->children.begin(); iter != a_root->children.end(); iter++ )
		{
			float childDist;
			auto avNode = iter->get();
			if( avNode )
			{
				auto node = avNode->AsNode();
				if( node )
				{
					auto childHit = FindClosestHitNode( node, a_pos, childDist, a_isPlayer );

					if( childDist < childMinDist )
					{
						childMinDist = childDist;
						childNode = childHit;
					}
				}
			}
		}
	}


	// Only check against node with collision object
	// Or if player is in first person mode then all of the nodes will not having any collision object
	if( a_root->collisionObject || a_isPlayer )
	{
		// Do not check excluded node
		if( !std::regex_match( a_root->name.c_str(), g_sExcludeRegexp ) )
		{
			if( !a_isPlayer || std::regex_match( a_root->name.c_str(), g_PlayerNodes ) )
			{
				auto* translation = &a_root->world.translate;
				float dx = translation->x - a_pos->x;
				float dy = translation->y - a_pos->y;
				float dz = translation->z - a_pos->z;
				a_dist = dx * dx + dy * dy + dz * dz;

				if( childMinDist < a_dist )
				{
					a_dist = childMinDist;
					return childNode;
				}

				return a_root->AsNode();
			}
		}
	}

	if( childNode )
	{
		a_dist = childMinDist;
		return childNode;
	}

	a_dist = 1000000;
	return NULL;
}

#pragma warning(pop)
