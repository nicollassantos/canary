local mType = Game.createMonsterType("Renegade Knight")
local monster = {}

monster.description = "a renegade knight"
monster.experience = 1200
monster.outfit = {
	lookType = 268,
	lookHead = 97,
	lookBody = 113,
	lookLegs = 76,
	lookFeet = 98,
	lookAddons = 2,
	lookMount = 0,
}

monster.raceId = 1146
monster.Bestiary = {
	class = "Human",
	race = BESTY_RACE_HUMAN,
	toKill = 1000,
	FirstUnlock = 50,
	SecondUnlock = 500,
	CharmsPoints = 25,
	Stars = 3,
	Occurrence = 0,
	Locations = "Old Fortress (north of Edron), Old Masonry, Forbidden Temple (Carlin).",
}

monster.health = 1450
monster.maxHealth = 1450
monster.race = "blood"
monster.corpse = 22020
monster.speed = 135
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
	rewardBoss = false,
	illusionable = false,
	canPushItems = true,
	canPushCreatures = false,
	staticAttackChance = 90,
	targetDistance = 1,
	runHealth = 0,
	healthHidden = false,
	isBlockable = false,
	canWalkOnEnergy = false,
	canWalkOnFire = false,
	canWalkOnPoison = false,
}

monster.light = {
	level = 0,
	color = 0,
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{ text = "I'll teach you a lesson!", yell = false },
	{ text = "Feel my steel!", yell = false },
	{ text = "Take this!", yell = false },
	{ text = "Let's see how good you are!", yell = false },
	{ text = "A challenge at last!", yell = false },
}

monster.loot = {
	{ id = 3447, chance = 90450, maxCount = 10 }, -- arrow
	{ id = 3031, chance = 75410, maxCount = 100 }, -- gold coin
	{ name = "two handed sword", chance = 5121 },
	{ name = "small ruby", chance = 3691 },
	{ name = "red piece of cloth", chance = 1784 },
	{ id = 3592, chance = 1210 }, -- grapes
	{ id = 239, chance = 7051 }, -- great health potion
	{ id = 3577, chance = 9307, maxCount = 2 }, -- meat
	{ id = 7364, chance = 1210, maxCount = 4 }, -- sniper arrow
	{ name = "war hammer", chance = 991 },
	{ id = 3381, chance = 661 }, -- crown armor
	{ id = 3385, chance = 562 }, -- crown helmet
	{ id = 3419, chance = 210 }, -- crown shield
	{ name = "piggy bank", chance = 113 },
	{ id = 3382, chance = 585 }, -- crown legs
	{ id = 3280, chance = 529 }, -- fire sword
	{ id = 3269, chance = 1610 }, -- halberd
	{ id = 3658, chance = 510 }, -- red rose
	{ id = 3003, chance = 1510 }, -- rope
	{ id = 2815, chance = 910 }, -- scroll
	{ id = 11510, chance = 6060 }, -- scroll of heroic deeds
	{ id = 11450, chance = 910 }, -- small notebook
	{ id = 3004, chance = 510 }, -- wedding ring
}

monster.attacks = {
	{ name = "melee", interval = 2000, chance = 100, minDamage = 10, maxDamage = -175 },
}

monster.defenses = {
	defense = 50,
	armor = 38,
	mitigation = 2.31,
	{ name = "combat", interval = 4000, chance = 25, type = COMBAT_HEALING, minDamage = 200, maxDamage = 250, effect = CONST_ME_MAGIC_BLUE, target = false },
}

monster.elements = {
	{ type = COMBAT_PHYSICALDAMAGE, percent = 15 },
	{ type = COMBAT_ENERGYDAMAGE, percent = 35 },
	{ type = COMBAT_EARTHDAMAGE, percent = 20 },
	{ type = COMBAT_FIREDAMAGE, percent = 20 },
	{ type = COMBAT_LIFEDRAIN, percent = 0 },
	{ type = COMBAT_MANADRAIN, percent = 0 },
	{ type = COMBAT_DROWNDAMAGE, percent = 0 },
	{ type = COMBAT_ICEDAMAGE, percent = 0 },
	{ type = COMBAT_HOLYDAMAGE, percent = 50 },
	{ type = COMBAT_DEATHDAMAGE, percent = -10 },
}

monster.immunities = {
	{ type = "paralyze", condition = true },
	{ type = "outfit", condition = false },
	{ type = "invisible", condition = true },
	{ type = "bleed", condition = false },
}

mType:register(monster)
