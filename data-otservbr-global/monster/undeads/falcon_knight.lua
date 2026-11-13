local mType = Game.createMonsterType("Falcon Knight")
local monster = {}

monster.description = "a falcon knight"
monster.experience = 6300
monster.outfit = {
	lookType = 1071,
	lookHead = 57,
	lookBody = 96,
	lookLegs = 38,
	lookFeet = 105,
	lookAddons = 1,
	lookMount = 0,
}

monster.raceId = 1646
monster.Bestiary = {
	class = "Undead",
	race = BESTY_RACE_UNDEAD,
	toKill = 2500,
	FirstUnlock = 100,
	SecondUnlock = 1000,
	CharmsPoints = 50,
	Stars = 4,
	Occurrence = 0,
	Locations = "Falcon Bastion.",
}

monster.health = 9000
monster.maxHealth = 9000
monster.race = "blood"
monster.corpse = 28621
monster.speed = 110
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10,
}

monster.strategiesTarget = {
	nearest = 100,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = false,
	illusionable = false,
	canPushItems = true,
	canPushCreatures = true,
	staticAttackChance = 90,
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

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "Mmmhaarrrgh!", yell = false },
}

monster.loot = {
	{ id = 3582, chance = 84530, maxCount = 3 }, -- ham
	{ name = "soul orb", chance = 41250 },
	{ name = "great mana potion", chance = 33000, maxCount = 3 },
	{ name = "great health potion", chance = 33000, maxCount = 3 },
	{ name = "flask of demonic blood", chance = 30000, maxCount = 4 },
	{ name = "small amethyst", chance = 29939, maxCount = 3 },
	{ name = "assassin star", chance = 29901, maxCount = 10 },
	{ name = "small diamond", chance = 17588, maxCount = 3 },
	{ name = "small ruby", chance = 18336, maxCount = 3 },
	{ name = "small emerald", chance = 18209, maxCount = 3 },
	{ name = "onyx arrow", chance = 17918, maxCount = 15 },
	{ name = "small topaz", chance = 6011, maxCount = 3 },
	{ name = "titan axe", chance = 3000 },
	{ id = 282, chance = 3652 }, -- giant shimmering pearl (brown)
	{ name = "spiked squelcher", chance = 2200 },
	{ name = "knight armor", chance = 1980 },
	{ name = "falcon crest", chance = 1250 },
	{ name = "war axe", chance = 1230 },
	{ name = "violet gem", chance = 1060 },
	{ name = "damaged armor plates", chance = 990 },
	{ name = "green gem", chance = 1243 },
	{ name = "golden armor", chance = 840 },
	{ name = "mastermind shield", chance = 620 },
	{ name = "heavy mace", chance = 460 },
	{ id = 3481, chance = 370 }, -- closed trap
	{ id = 3019, chance = 266 }, -- demonbone amulet
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -400 },
	{ name = "combat", interval = 2000, chance = 18, type = COMBAT_EARTHDAMAGE, minDamage = -400, maxDamage = -500, radius = 2, effect = CONST_ME_GROUNDSHAKER, target = false },
	{ name = "combat", interval = 2000, chance = 14, type = COMBAT_HOLYDAMAGE, minDamage = -290, maxDamage = -360, length = 5, spread = 0, effect = CONST_ME_BLOCKHIT, target = false },
}

monster.defenses = {
	defense = 86,
	armor = 86,
	mitigation = 3.78,
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 30 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 0 },
	{ type = COMBAT_EARTHDAMAGE, percent = 0 },
	{ type = COMBAT_FIREDAMAGE, percent = 0 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = -10 },
	{ type = COMBAT_DEATHDAMAGE, percent = 50 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = false },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
