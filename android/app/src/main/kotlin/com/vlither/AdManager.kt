package com.vlither

import android.app.Activity
import android.content.Context
import android.util.Log
import android.widget.Toast
import com.google.android.gms.ads.AdError
import com.google.android.gms.ads.AdRequest
import com.google.android.gms.ads.FullScreenContentCallback
import com.google.android.gms.ads.LoadAdError
import com.google.android.gms.ads.MobileAds
import com.google.android.gms.ads.OnUserEarnedRewardListener
import com.google.android.gms.ads.rewarded.RewardedAd
import com.google.android.gms.ads.rewarded.RewardedAdLoadCallback
import com.google.android.ump.ConsentInformation
import com.google.android.ump.ConsentRequestParameters
import com.google.android.ump.UserMessagingPlatform

/**
 * Wraps AdMob rewarded-ad logic + the GDPR/UK consent flow (UMP SDK) in one place.
 *
 * ── BEFORE YOU PUBLISH TO THE PLAY STORE ───────────────────────────────
 * REWARDED_AD_UNIT_ID currently resolves to Google's official TEST ad unit
 * ID. It always serves a sample ad, is not tied to any AdMob account, and
 * is completely safe to ship by accident — it just earns no real money.
 *
 * When you're actually ready to publish:
 *   1. AdMob console -> Apps -> Vlither -> Ad units -> Rewarded -> create one.
 *   2. Paste that ad unit ID into REAL_REWARDED_AD_UNIT_ID below.
 *   3. AdMob console -> Privacy & messaging -> create + publish a GDPR
 *      message (EEA/UK) — the UMP SDK below will only show a real form
 *      once that exists; until then it just no-ops for those users.
 *   4. Build a release build and confirm the warning in
 *      verifyNotShippingTestAdsInRelease no longer fires (Logcat: "VlitherAds").
 * ────────────────────────────────────────────────────────────────────────
 */
object AdManager {
    private const val TAG = "VlitherAds"

    // Google's official sample/test rewarded ad unit ID for Android.
    // https://developers.google.com/admob/android/test-ads
    private const val TEST_REWARDED_AD_UNIT_ID =
        "ca-app-pub-3940256099942544/5224354917"

    // Your real rewarded ad unit ID (AdMob console -> Vlither -> Ad units).
    // Leave this set — it's what takes you off test ads.
    private const val REAL_REWARDED_AD_UNIT_ID = "ca-app-pub-9185462985805279/2901530505"

    private val rewardedAdUnitId: String
        get() = REAL_REWARDED_AD_UNIT_ID.ifBlank { TEST_REWARDED_AD_UNIT_ID }

    private var rewardedAd: RewardedAd? = null
    private var isLoading = false
    private var mobileAdsStarted = false
    private var consentInformation: ConsentInformation? = null

    /**
     * Call once, early (e.g. MainActivity.onCreate). Gathers/refreshes consent
     * first (required every launch per Google's UMP guidance), then starts the
     * Mobile Ads SDK and invokes [onReady] only once ads can actually be
     * requested. Safe to call more than once.
     */
    fun initialize(activity: Activity, onReady: () -> Unit) {
        verifyNotShippingTestAdsInRelease(activity)

        val info = UserMessagingPlatform.getConsentInformation(activity)
        consentInformation = info

        val params = ConsentRequestParameters.Builder().build()
        info.requestConsentInfoUpdate(
            activity,
            params,
            {
                UserMessagingPlatform.loadAndShowConsentFormIfRequired(activity) { formError ->
                    if (formError != null) {
                        Log.w(TAG, "Consent form error (${formError.errorCode}): ${formError.message}")
                    }
                    if (info.canRequestAds()) {
                        startMobileAdsSdk(activity, onReady)
                    } else {
                        Log.w(TAG, "Ads not requestable after consent flow — skipping ad init this session.")
                    }
                }
            },
            { requestError ->
                Log.w(TAG, "Consent info update failed: ${requestError.message}")
                // Fall back to a previously cached consent decision, if any,
                // so a transient network hiccup doesn't permanently block ads.
                if (info.canRequestAds()) {
                    startMobileAdsSdk(activity, onReady)
                }
            }
        )
    }

    private fun startMobileAdsSdk(context: Context, onReady: () -> Unit) {
        if (mobileAdsStarted) { onReady(); return }
        mobileAdsStarted = true
        Thread {
            MobileAds.initialize(context.applicationContext) { status ->
                Log.d(TAG, "MobileAds SDK initialized: $status")
            }
        }.start()
        onReady()
    }

    /** Whether the "Privacy Options" entry point must be shown (EEA/UK users only). */
    fun isPrivacyOptionsRequired(): Boolean =
        consentInformation?.privacyOptionsRequirementStatus ==
            ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED

    /** Lets the user revisit/change their consent choice later. */
    fun showPrivacyOptionsForm(activity: Activity) {
        UserMessagingPlatform.showPrivacyOptionsForm(activity) { formError ->
            if (formError != null) {
                Log.w(TAG, "Privacy options form error: ${formError.message}")
            }
        }
    }

    /** Kick off (or re-kick off) loading the next rewarded ad in the background. */
    fun preload(context: Context, onLoaded: (() -> Unit)? = null) {
        if (rewardedAd != null) {
            onLoaded?.invoke()
            return
        }
        if (isLoading) return
        isLoading = true

        RewardedAd.load(
            context,
            rewardedAdUnitId,
            AdRequest.Builder().build(),
            object : RewardedAdLoadCallback() {
                override fun onAdLoaded(ad: RewardedAd) {
                    isLoading = false
                    rewardedAd = ad
                    Log.d(TAG, "Rewarded ad loaded.")
                    onLoaded?.invoke()
                }

                override fun onAdFailedToLoad(error: LoadAdError) {
                    isLoading = false
                    rewardedAd = null
                    Log.w(TAG, "Rewarded ad failed to load: ${error.message}")
                }
            }
        )
    }

    fun isReady(): Boolean = rewardedAd != null

    /**
     * Shows the preloaded rewarded ad, if one is ready.
     * onReward fires only if the user actually earned the reward (watched
     * it through). onClosed fires whenever the ad closes either way.
     * onUnavailable fires immediately if no ad was ready to show.
     */
    fun show(
        activity: Activity,
        onReward: () -> Unit,
        onClosed: () -> Unit = {},
        onUnavailable: () -> Unit = {}
    ) {
        val ad = rewardedAd
        if (ad == null) {
            onUnavailable()
            preload(activity)
            return
        }

        ad.fullScreenContentCallback = object : FullScreenContentCallback() {
            override fun onAdDismissedFullScreenContent() {
                rewardedAd = null
                onClosed()
                preload(activity)
            }

            override fun onAdFailedToShowFullScreenContent(error: AdError) {
                rewardedAd = null
                Log.w(TAG, "Rewarded ad failed to show: ${error.message}")
                onUnavailable()
                preload(activity)
            }
        }

        ad.show(activity, OnUserEarnedRewardListener { rewardItem ->
            Log.d(TAG, "User earned reward: ${rewardItem.amount} ${rewardItem.type}")
            onReward()
        })
    }

    /**
     * Loud, hard-to-miss warning if a RELEASE build is still pointing at
     * the test ad unit ID. Does not block the build — just makes sure you
     * can't possibly miss it before you get anywhere near the Play Console.
     */
    private fun verifyNotShippingTestAdsInRelease(context: Context) {
        if (!BuildConfig.DEBUG && rewardedAdUnitId == TEST_REWARDED_AD_UNIT_ID) {
            val msg = "WARNING: release build is still using the TEST ad " +
                "unit ID. Set REAL_REWARDED_AD_UNIT_ID in AdManager.kt " +
                "before publishing to the Play Store."
            Log.e(TAG, msg)
            try {
                Toast.makeText(context, msg, Toast.LENGTH_LONG).show()
            } catch (e: Exception) {
                // No UI thread / window yet this early — the Logcat line above is enough.
            }
        }
    }
}
