local mType = Game.createMonsterType("Ravenous Hunger")
local monster = {}

monster.description = "Ravenous Hunger"
monster.experience = 0
monster.outfit = {
	lookType = 556,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0,
}

monster.events = {
	"CultsOfTibiaBossDeath",
}

monster.bosstiary = {
	bossRaceId = 1427,
	bossRace = RARITY_ARCHFOE,
}

monster.health = 100000
monster.maxHealth = 100000
monster.race = "blood"
monster.corpse = 6323
monster.speed = 140
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 20,
}

monster.strategiesTarget = {
	nearest = 70,
	health = 10,
	damage = 10,
	random = 10,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = true,
	illusionable = false,
	canPushItems = true,
	canPushCreatures = true,
	staticAttackChance = 95,
	targetDistance = 1,
	runHealth = 0,
	healthHidden = false,
	isBlockable = false,
	canWalkOnEnergy = true,
	canWalkOnFire = true,
	canWalkOnPoison = true,
}

monster.light = {
	level = 0,
	color = 0,
}

monster.summon = {
	maxSummons = 4,
	summons = {
		{ name = "Mutated Bat", chance = 100, interval = 2000, count = 1 },
		{ name = "Mutated Bat", chance = 100, interval = 2000, count = 1 },
		{ name = "Mutated Bat", chance = 100, interval = 2000, count = 1 },
		{ name = "Mutated Bat", chance = 100, interval = 2000, count = 1 },
	},
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "SU-*burp* SUFFEEER!", yell = false },
}

monster.loot = {
	{ name = "bed of nails", chance = 99145 },
	{ name = "small sapphire", chance = 21000, maxCount = 19 },
	{ name = "great spirit potion", chance = 48718, maxCount = 18 },
	{ name = "yellow gem", chance = 12000 },
	{ id = 282, chance = 15385 }, -- giant shimmering pearl (brown)
	{ name = "platinum coin", chance = 68299, maxCount = 48 },
	{ name = "lightning legs", chance = 18000 },
	{ name = "sacred tree amulet", chance = 70085 },
	{ name = "wood cape", chance = 58120 },
	{ name = "gold token", chance = 26496 },
	{ name = "gold coin", chance = 100000, maxCount = 378 },
	{ name = "small emerald", chance = 19000, maxCount = 19 },
	{ name = "great mana potion", chance = 58974, maxCount = 16 },
	{ id = 3039, chance = 12000 }, -- red gem
	{ name = "oriental shoes", chance = 11000 },
	{ name = "torn shirt", chance = 42000 },
	{ name = "fig leaf", chance = 51282 },
	{ name = "luminous orb", chance = 15385 },
	{ name = "wooden spellbook", chance = 11966 },
	{ name = "elven legs", chance = 1709 },
	{ name = "small diamond", chance = 21000, maxCount = 19 },
	{ name = "ultimate health potion", chance = 52991, maxCount = 16 },
	{ name = "energy bar", chance = 100000, maxCount = 9 },
	{ name = "green gem", chance = 24786, maxCount = 2 },
	{ name = "broken key ring", chance = 12821 },
	{ name = "muck rod", chance = 59829 },
	{ name = "mysterious remains", chance = 100000 },
	{ name = "cobra crown", chance = 2564 },
	{ name = "silver token", chance = 20513 },
	{ name = "elven mail", chance = 3000 },
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -900 },
}

monster.defenses = {
	defense = 50,
	armor = 35,
	--	mitigation = ???,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 0 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 0 },
	{ type = COMBAT_EARTHDAMAGE, percent = 100 },
	{ type = COMBAT_FIREDAMAGE, percent = 0 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = 0 },
	{ type = COMBAT_DEATHDAMAGE, percent = 100 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = false },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
