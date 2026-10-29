/* Armor Games intro (neutralized).
 * The stock version of this file overlaid the whole canvas with a clickable
 * <a href="http://armor.ag/MoreGames"> wrapper and played armorIntro.ogv.
 * armor.ag is dead — it now redirects to itch.io — so the link is gone, and
 * the intro video (armorIntro.ogv/.m4v/.webm) was never part of this game's
 * assets anyway. The game only asks "is the intro done?"; we answer yes.
 * playIntro()/hasPlayedIntro() keep their signatures so the game code
 * (obj_splash_armor) works unmodified.                                  */

var donePlayingIntro = true;

/** Play the Armor Games intro (removed: dead redirect link, missing video). */
function playIntro(windowHandleName, directory) {
    donePlayingIntro = true;
}

/** Whether we're done playing the armor games intro (always true now). */
function hasPlayedIntro() {
    return donePlayingIntro;
}
