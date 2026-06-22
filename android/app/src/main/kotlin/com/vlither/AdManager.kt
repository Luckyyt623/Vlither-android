package com.vlither

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
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
 * The real ad unit ID lives in android/app/keystore.properties (gitignored,
 * local-only) or the ADMOB_AD_UNIT_ID GitHub Actions secret in CI — never
 * hardcoded here. If neither is set, this automatically falls back to
 * Google's test ad unit ID, so a build with no secret configured is always
 * safe to run.
 *
 * A brand-new ad unit can take up to ~1 hour to start filling — failed
 * loads during that window are expected and will auto-retry below.
 */
object AdManager {
    private const val TAG = "VlitherAds"

    // Google's official sample/test rewarded ad unit ID for Android.
    // https://developers.google.com/admob/android/test-ads
    private const val TEST_REWARDED_AD_UNIT_ID =
        "ca-app-pub-3940256099942544/5224354917"

    // Real rewarded ad unit ID — comes from BuildConfig, which Gradle fills in
    // from android/app/keystore.properties (gitignored) or, in CI, from the
    // ADMOB_AD_UNIT_ID repo secret. Never hardcoded in source.
    private val rewardedAdUnitId: String
        get() = BuildConfig.ADMOB_AD_UNIT_ID.ifBlank { TEST_REWARDED_AD_UNIT_ID }

    private var rewardedAd: RewardedAd? = null
    private var isLoading = false
    private var mobileAdsStarted = false
    private var consentInformation: ConsentInformation? = null

    private var statusListener: (() -> Unit)? = null
    private var retryAttempt = 0
    private val retryHandler = Handler(Looper.getMainLooper())

    /**
     * Called whenever ad status changes — load succeeded, load failed
     * (about to retry), shown, or dismissed. Wire this to your UI refresh
     * (e.g. MainActivity calls this once in onCreate with refreshUnlockUi).
     */
    fun setStatusListener(listener: () -> Unit) {
        statusListener = listener
    }

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

    /**
     * Kick off (or re-kick off) loading the next rewarded ad in the background.
     * On failure this automatically retries with backoff (1/2/4/8 min, capped
     * at 5 min) — important for a brand-new ad unit that can take up to ~1hr
     * to start filling. Always uses applicationContext internally so a long
     * retry chain never holds on to a destroyed Activity.
     */
    fun preload(context: Context) {
        if (rewardedAd != null || isLoading) return
        isLoading = true
        val appContext = context.applicationContext

        RewardedAd.load(
            appContext,
            rewardedAdUnitId,
            AdRequest.Builder().build(),
            object : RewardedAdLoadCallback() {
                override fun onAdLoaded(ad: RewardedAd) {
                    isLoading = false
                    retryAttempt = 0
                    rewardedAd = ad
                    Log.d(TAG, "Rewarded ad loaded.")
                    statusListener?.invoke()
                }

                override fun onAdFailedToLoad(error: LoadAdError) {
                    isLoading = false
                    rewardedAd = null
                    Log.w(TAG, "Rewarded ad failed to load (code ${error.code}): ${error.message}")
                    statusListener?.invoke()
                    scheduleRetry(appContext)
                }
            }
        )
    }

    private fun scheduleRetry(appContext: Context) {
        retryAttempt++
        val delayMs = (60_000L * (1L shl minOf(retryAttempt, 3))).coerceAtMost(300_000L)
        Log.d(TAG, "Retrying ad load in ${delayMs / 1000}s (attempt $retryAttempt)")
        retryHandler.postDelayed({ preload(appContext) }, delayMs)
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
                "unit ID. Set ADMOB_AD_UNIT_ID in keystore.properties (or " +
                "the CI secret) before publishing to the Play Store."
            Log.e(TAG, msg)
            try {
                Toast.makeText(context, msg, Toast.LENGTH_LONG).show()
            } catch (e: Exception) {
                // No UI thread / window yet this early — the Logcat line above is enough.
            }
        }
    }
}
