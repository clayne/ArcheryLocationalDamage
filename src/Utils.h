#pragma once
#pragma warning(push)
#pragma warning(disable: 4505)

struct StringFilter
{
	std::string		str;
	bool			isNegate = false;
};

using StringFilterList = std::vector<StringFilter>;

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

static bool FormHasKeywords( RE::BGSKeywordForm* a_form, StringFilterList& a_keywords, std::regex* a_regex = NULL )
{
	for( auto& keyword : a_keywords )
	{
		bool hasKeyword = a_form->HasKeywordString( keyword.str );

		if( keyword.isNegate )
			hasKeyword = !hasKeyword;

		if( !hasKeyword )
		{
			// Skip check for current keyword if regex not matched
			if( a_regex && !std::regex_match( keyword.str, *a_regex ) )
				continue;
				
			return false;
		}
	}

	return true;
}

static bool ActiveEffectsHasKeywords( RE::Actor* a_actor, StringFilterList& a_keywords )
{
	// Search active effects if both actor and armor has none of the keyword
	auto activeEffects = a_actor->GetActiveEffectList();
	for( auto activeEffect : *activeEffects )
	{
		// Effect must active to count for keyword matching
		if( activeEffect->flags.none( RE::ActiveEffect::Flag::kInactive ) &&
			FormHasKeywords( activeEffect->effect->baseEffect, a_keywords ) )
			return true;
	}

	return false;
}

static std::regex g_armorKeywordRegex( "Armor.*" );
static bool ActorHasKeywords( RE::Actor* a_actor, StringFilterList& a_keywords )
{
	bool actorHasKeyword	= true;
	bool armorHasKeyword	= true;
	bool includeArmor		= false;

	for( auto& keyword : a_keywords )
	{
		if( !std::regex_match( keyword.str, g_armorKeywordRegex ) )
			actorHasKeyword = actorHasKeyword && a_actor->HasKeywordString( keyword.str );
		else
			includeArmor = true;
	}

	if( includeArmor )
	{
		armorHasKeyword = false;

		const auto inv = a_actor->GetInventory([](RE::TESBoundObject& a_object) {
			return a_object.IsArmor();
			});

		for( const auto& [item, invData] : inv ) 
		{
			const auto& [count, entry] = invData;
			if( count > 0 && entry->IsWorn() ) 
			{
				const auto armor = item->As<RE::TESObjectARMO>();
				if( armor ) 
				{
					armorHasKeyword = FormHasKeywords( armor, a_keywords, &g_armorKeywordRegex );

					if( armorHasKeyword ) break;
				}
			}
		}
	}

	return actorHasKeyword && armorHasKeyword;
}

#pragma warning(pop)
